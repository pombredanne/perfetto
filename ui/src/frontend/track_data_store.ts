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

import {TrackData} from './track';

/**
 * Maps track IDs to the data a track needs to render. Track data is assumed to
 * be much higher in volume, and therefore we assume TrackDataStore does not
 * update as frequently.
 *
 * Data for a particular track can only be set all at once. It is not possible
 * to append to already existing data.
 */
export class TrackDataStore {
  private trackIdToData: Map<string, TrackData>;

  constructor() {
    this.trackIdToData = new Map<string, TrackData>();
  }

  getTrackData(trackId: string): TrackData|undefined {
    // TODO: Validation?
    return this.trackIdToData.get(trackId);
  }

  storeData(data: TrackData): void {
    // TODO: Validation?
    this.trackIdToData.set(data.id, data);
  }
}