// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import {fromNs} from '../../common/time';
import {
  TrackController,
  trackControllerRegistry
} from '../../controller/track_controller';

import {
  Config,
  Data,
  PROCESS_SUMMARY_TRACK,
} from './common';

class ProcessSummaryTrackController extends TrackController<Config, Data> {
  static readonly kind = PROCESS_SUMMARY_TRACK;
  private busy = false;
  private setup = false;

  onBoundsChange(start: number, end: number, resolution: number): void {
    this.update(start, end, resolution);
  }

  // Returns a track id representation valid for use in sql table names.
  private get sqlTrackId(): string {
    // Track ID can be UUID but '-' is not valid for sql table name.
    return this.trackId.split('-').join('_');
  }

  private async update(start: number, end: number, resolution: number):
      Promise<void> {
    // TODO: we should really call TraceProcessor.Interrupt() at this point.
    if (this.busy) return;
    this.busy = true;

    const startNs = Math.round(start * 1e9);
    const endNs = Math.round(end * 1e9);

    if (this.setup === false) {
      await this.query(
          `create virtual table window_${this.sqlTrackId} using window;`);
      const threadQuery = await this.query(
          `select utid from thread where upid=${this.config.upid}`);
      const utids = threadQuery.columns[0].longValues! as number[];
      const processSliceView = `process_slice_view_${this.sqlTrackId}`;
      await this.query(
          `create view ${processSliceView} as ` +
          // 0 as cpu is a dummy column to perform span join on.
          `select ts, dur/${utids.length} as dur, 0 as cpu ` +
          `from slices where depth = 0 and utid in ` +
          // TODO(dproy): This query is faster if we write it as x < utid < y.
          `(${utids.join(',')})`);
      await this.query(`create virtual table span_${this.sqlTrackId}
                     using span(${processSliceView}, window_${
                                                              this.sqlTrackId
                                                            }, cpu);`);
      this.setup = true;
    }

    // |resolution| is in s/px we want # ns for 10px window:
    const bucketSizeNs = Math.round(resolution * 10 * 1e9);
    const windowStartNs = Math.floor(startNs / bucketSizeNs) * bucketSizeNs;
    const windowDurNs = endNs - windowStartNs;

    this.query(`update window_${this.sqlTrackId} set
      window_start=${windowStartNs},
      window_dur=${windowDurNs},
      quantum=${bucketSizeNs}
      where rowid = 0;`);

    this.publish(await this.computeSummary(
        fromNs(windowStartNs), end, resolution, bucketSizeNs));
    this.busy = false;
  }

  private async computeSummary(
      start: number, end: number, resolution: number,
      bucketSizeNs: number): Promise<Data> {
    const startNs = Math.round(start * 1e9);
    const endNs = Math.round(end * 1e9);
    const numBuckets = Math.ceil((endNs - startNs) / bucketSizeNs);

    const query = `select
        quantum_ts as bucket,
        sum(dur)/cast(${bucketSizeNs} as float) as utilization
        from span_${this.sqlTrackId}
        group by quantum_ts`;

    const beforeQuery = performance.now();
    const rawResult = await this.query(query);
    console.info('Summary query: ', query);
    console.info(
        'Summary query execution time: ', performance.now() - beforeQuery);
    const numRows = +rawResult.numRecords;

    const summary: Data = {
      start,
      end,
      resolution,
      bucketSizeSeconds: fromNs(bucketSizeNs),
      utilizations: new Float64Array(numBuckets),
    };
    const cols = rawResult.columns;
    for (let row = 0; row < numRows; row++) {
      const bucket = +cols[0].longValues![row];
      summary.utilizations[bucket] = +cols[1].doubleValues![row];
    }
    return summary;
  }

  // TODO(dproy); Dedup with other controllers.
  private async query(query: string) {
    const result = await this.engine.query(query);
    if (result.error) {
      console.error(`Query error "${query}": ${result.error}`);
      throw new Error(`Query error "${query}": ${result.error}`);
    }
    return result;
  }

  onDestroy(): void {
    console.log('Process summary being destroyed!');
    if (this.setup) {
      this.query(`drop table window_${this.sqlTrackId}`);
      this.query(`drop table span_${this.sqlTrackId}`);
      this.setup = false;
    }
  }
}

trackControllerRegistry.register(ProcessSummaryTrackController);
