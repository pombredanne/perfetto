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
  PROCESS_SCHEDULING_TRACK_KIND,
  SliceData,
  SummaryData
} from './common';

class ProcessSchedulingTrackController extends TrackController<Config, Data> {
  static readonly kind = PROCESS_SCHEDULING_TRACK_KIND;
  private busy = false;
  private setup = false;
  // private utids: number[] = [];

  onBoundsChange(start: number, end: number, resolution: number): void {
    this.update(start, end, resolution);
  }

  private async update(start: number, end: number, resolution: number):
      Promise<void> {
    // TODO: we should really call TraceProcessor.Interrupt() at this point.
    if (this.busy) return;

    const startNs = Math.round(start * 1e9);
    const endNs = Math.round(end * 1e9);

    this.busy = true;
    if (this.setup === false) {
      await this.query(
          `create virtual table ${this.tableName('window')} using window;`);
      // const threadQuery = await this.query(
      //    `select utid from thread where upid=${this.config.upid}`);
      // this.utids = threadQuery.columns[0].longValues! as number[];
      await this.query(`create view ${this.tableName('process')} as
          select ts, dur, cpu, utid, upid from sched join (select utid, upid from
            thread where upid = ${this.config.upid}) using(utid);`);
      await this.query(`create virtual table ${this.tableName('span')}
              using span_join(${this.tableName('process')} PARTITIONED cpu,
                              ${this.tableName('window')} PARTITIONED cpu);`);
      this.setup = true;
    }

    // |resolution| is in s/px (to nearest power of 10) asumming a display
    // of ~1000px 0.001 is 1s.
    const isQuantized = resolution >= 0.001;
    // |resolution| is in s/px we want # ns for 10px window:
    const bucketSizeNs = Math.round(resolution * 10 * 1e9);
    let windowStartNs = startNs;
    if (isQuantized) {
      windowStartNs = Math.floor(windowStartNs / bucketSizeNs) * bucketSizeNs;
    }
    const windowDurNs = Math.max(1, endNs - windowStartNs);

    this.query(`update ${this.tableName('window')} set
      window_start=${windowStartNs},
      window_dur=${windowDurNs},
      quantum=${isQuantized ? bucketSizeNs : 0}
      where rowid = 0;`);

    if (isQuantized) {
      this.publish(await this.computeSummary(
          fromNs(windowStartNs), end, resolution, bucketSizeNs));
    } else {
      this.publish(
          await this.computeSlices(fromNs(windowStartNs), end, resolution));
    }
    this.busy = false;
  }

  private async computeSummary(
      start: number, end: number, resolution: number,
      bucketSizeNs: number): Promise<SummaryData> {
    const startNs = Math.round(start * 1e9);
    const endNs = Math.round(end * 1e9);
    const numBuckets = Math.ceil((endNs - startNs) / bucketSizeNs);

    const query = `select
        quantum_ts as bucket,
        sum(dur)/cast(${bucketSizeNs * 8} as float) as utilization
        from ${this.tableName('span')}
        where upid = ${this.config.upid}
        and cpu < 8
        group by quantum_ts`;

    const rawResult = await this.query(query);
    const numRows = +rawResult.numRecords;

    const summary: Data = {
      kind: 'summary',
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

  private async computeSlices(start: number, end: number, resolution: number):
      Promise<SliceData> {
    // TODO(hjd): Remove LIMIT
    const LIMIT = 10000;

    const query = `select ts,dur,utid from ${this.tableName('span')}
        join
        (select utid from thread where upid = ${this.config.upid})
        using(utid)
        where cpu = 0
        limit ${LIMIT};`;
    const rawResult = await this.query(query);

    const numRows = +rawResult.numRecords;
    const slices: SliceData = {
      kind: 'slice',
      start,
      end,
      resolution,
      starts: new Float64Array(numRows),
      ends: new Float64Array(numRows),
      utids: new Uint32Array(numRows),
    };

    const cols = rawResult.columns;
    for (let row = 0; row < numRows; row++) {
      const startSec = fromNs(+cols[0].longValues![row]);
      slices.starts[row] = startSec;
      slices.ends[row] = startSec + fromNs(+cols[1].longValues![row]);
      slices.utids[row] = +cols[2].longValues![row];
    }
    if (numRows === LIMIT) {
      slices.end = slices.ends[slices.ends.length - 1];
    }
    return slices;
  }

  private async query(query: string) {
    const result = await this.engine.query(query);
    if (result.error) {
      console.error(`Query error "${query}": ${result.error}`);
      throw new Error(`Query error "${query}": ${result.error}`);
    }
    return result;
  }

  onDestroy(): void {
    if (this.setup) {
      this.query(`drop table ${this.tableName('window')}`);
      this.query(`drop table ${this.tableName('span')}`);
      this.setup = false;
    }
  }
}

trackControllerRegistry.register(ProcessSchedulingTrackController);
