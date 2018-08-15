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
  Engine,
  PublishFn,
  TrackController,
  TrackState
} from '../../controller/track_controller';
import {
  trackControllerRegistry
} from '../../controller/track_controller_registry';

import {CpuSliceTrackData, TRACK_KIND} from './common';

// TODO(hjd): Too much bolierplate here. Prehaps TrackController/Track
// should be an interface and we provide a TrackControllerBase/TrackBase
// you can inherit from which does the basic things.
class CpuSliceTrackController extends TrackController {
  static readonly kind = TRACK_KIND;

  static create(config: TrackState, engine: Engine, publish: PublishFn):
      TrackController {
    return new CpuSliceTrackController(config.cpu, engine, publish);
  }

  private cpu: number;
  private engine: Engine;
  // TODO: This publish function should be better typed to only accept
  // CpuSliceTrackData. Perhaps we can do PublishFn<T>.
  private publish: PublishFn;
  private busy = false;

  constructor(cpu: number, engine: Engine, publish: PublishFn) {
    super();
    this.cpu = cpu;
    this.engine = engine;
    this.publish = publish;
  }

  onBoundsChange(start: number, end: number, resolution: number) {
    // TODO: we should really call TraceProcessor.Interrupt() at this point.
    if (this.busy) return;
    const LIMIT = 5000;

    // TODO: "ts >= start - dur" needs to be optimized, right now it causes a
    // full table scan.
    const query = 'select ts,dur,utid from sched ' +
        `where cpu = ${this.cpu} ` +
        `and ts >= ${Math.round(start * 1e9)} - dur ` +
        `and ts <= ${Math.round(end * 1e9)} ` +
        `and dur >= ${Math.round(resolution * 1e9)} ` +
        `order by ts ` +
        `limit ${LIMIT};`;

    if (this.cpu === 0) console.log('QUERY', query);

    this.busy = true;
    const promise = this.engine.rawQuery({'sqlQuery': query});

    promise.then(rawResult => {
      this.busy = false;
      if (rawResult.error) {
        throw new Error(`Query error "${query}": ${rawResult.error}`);
      }
      if (this.cpu === 0) console.log('QUERY DONE', query);

      const numRows = +rawResult.numRecords;

      const slices: CpuSliceTrackData = {
        start,
        end,
        resolution,
        starts: new Float64Array(numRows),
        ends: new Float64Array(numRows),
        utids: new Uint32Array(numRows),
      };

      for (let row = 0; row < numRows; row++) {
        const cols = rawResult.columns;
        const startSec = fromNs(+cols[0].longValues![row]);
        slices.starts[row] = startSec;
        slices.ends[row] = startSec + fromNs(+cols[1].longValues![row]);
        slices.utids[row] = +cols[2].longValues![row];
      }
      if (numRows === LIMIT) {
        slices.end = slices.ends[slices.ends.length - 1];
      }

      this.publish(slices);
    });
  }
}

trackControllerRegistry.register(CpuSliceTrackController);
