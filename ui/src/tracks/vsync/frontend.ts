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

import {requestTrackData} from '../../common/actions';
import {TrackState} from '../../common/state';
import {globals} from '../../frontend/globals';
import {Track} from '../../frontend/track';
import {trackRegistry} from '../../frontend/track_registry';

import {Config, Data, KIND} from './common';

// TODO(hjd): De-dupe this from ChromeSliceTrack, CpuSliceTrack and VsyncTrack.
const MARGIN_TOP = 5;
const RECT_HEIGHT = 30;

class VsyncTrack extends Track<Config, Data> {
  static readonly kind = KIND;
  static create(trackState: TrackState): VsyncTrack {
    return new VsyncTrack(trackState);
  }

  constructor(trackState: TrackState) {
    super(trackState);
  }

  renderCanvas(ctx: CanvasRenderingContext2D): void {
    const {timeScale, visibleWindowTime} = globals.frontendLocalState;

    const data = this.data();
    if (data === undefined) {
      globals.dispatch(requestTrackData(this.trackState.id, 1, 1, 1));
      return;
    }

    for (let i = 0; i < 2; i++) {
      ctx.fillStyle = ['blue', 'red'][i];
      for (let j = i; j < data.starts.length; j += 2) {
        if (data.ends[j] < visibleWindowTime.start) continue;
        if (data.starts[j] > visibleWindowTime.end) break;
        const startPx = timeScale.timeToPx(data.starts[j]);
        const endPx = timeScale.timeToPx(data.ends[j]);
        const widthPx = endPx - startPx;
        ctx.fillRect(startPx, MARGIN_TOP, widthPx, RECT_HEIGHT);
      }
    }
  }
}

trackRegistry.register(VsyncTrack);
