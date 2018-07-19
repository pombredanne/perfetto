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

import {TrackState} from '../common/state';
import {gTrackRegistry} from '../frontend/globals';
import {TrackImpl} from '../frontend/track';

import {CpuCounterTrack} from './cpu_counters/frontend';
import {CpuSliceTrack} from './cpu_slices/frontend';

/**
 * List of all the currently implemented tracks. When a new track is
 * implemented, the track class must be added here.
 */
const allTracks = [
  CpuCounterTrack,
  CpuSliceTrack,
];

// We store a function that calls the constructor and returns and instantiated
// TrackImpl instead of storing the TrackImpl subclass directly, because
// otherwise the values in the map has the type of abstract class TrackImpl when
// retrieved, which cannot be instantiated.
export type TrackCreator = (trackState: TrackState, width: number) => TrackImpl;

export function registerAllTracks() {
  for (const TrackClass of allTracks) {
    gTrackRegistry.set(
        TrackClass.type, (ts, width) => new TrackClass(ts, width));
  }
}
