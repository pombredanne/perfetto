// Copyright (C) 2019 The Android Open Source Project
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


import {search, searchEq} from '../../base/binary_search';
import {Actions} from '../../common/actions';
import {TrackState} from '../../common/state';
import {colorForState} from '../../frontend/colorizer';
import {globals} from '../../frontend/globals';
import {Track} from '../../frontend/track';
import {trackRegistry} from '../../frontend/track_registry';

import {
  Config,
  Data,
  THREAD_STATE_TRACK_KIND,
} from './common';

const MARGIN_TOP = 5;
const RECT_HEIGHT = 12;

class ThreadStateTrack extends Track<Config, Data> {
  static readonly kind = THREAD_STATE_TRACK_KIND;
  static create(trackState: TrackState): ThreadStateTrack {
    return new ThreadStateTrack(trackState);
  }

  constructor(trackState: TrackState) {
    super(trackState);
  }

  getHeight(): number {
    return 22;
  }

  renderCanvas(ctx: CanvasRenderingContext2D): void {
    const {timeScale, visibleWindowTime} = globals.frontendLocalState;
    const data = this.data();

    // If there aren't enough cached slices data in |data| request more to
    // the controller.
    const inRange = data !== undefined &&
        (visibleWindowTime.start >= data.start &&
         visibleWindowTime.end <= data.end);
    if (!inRange || data === undefined ||
        data.resolution !== globals.getCurResolution()) {
      globals.requestTrackData(this.trackState.id);
    }
    if (data === undefined) return;  // Can't possibly draw anything.

    for (let i = 0; i < data.startNs.length; i++) {
      const tStart = data.startNs[i];
      const tEnd = data.endNs[i];
      const state = data.strings[data.state[i]];
      if (tEnd <= visibleWindowTime.start || tStart >= visibleWindowTime.end) {
        continue;
      }
      if (tStart && tEnd) {
        const rectStart = timeScale.timeToPx(tStart);
        const rectEnd = timeScale.timeToPx(tEnd);
        const rectWidth = rectEnd - rectStart;
        if (rectWidth < 0.3) continue;
        const color = colorForState(state);
        ctx.fillStyle = `hsl(${color.h},${color.s}%,${color.l}%)`;
        ctx.fillRect(rectStart, MARGIN_TOP, rectEnd - rectStart, RECT_HEIGHT);
      }
    }

    const selection = globals.state.currentSelection;
    if (selection !== null && selection.kind === 'THREAD_STATE' &&
        selection.utid === this.config.utid) {
      const index = searchEq(data.startNs, selection.ts);
      if (index[0] !== index[1]) {
        const tStart = data.startNs[index[0]];
        const tEnd = data.endNs[index[0]];
        const state = data.strings[data.state[index[0]]];
        const rectStart = timeScale.timeToPx(tStart);
        const rectEnd = timeScale.timeToPx(tEnd);
        const color = colorForState(state);
        ctx.strokeStyle = `hsl(${color.h},${color.s}%,${color.l * 0.7}%)`;
        ctx.beginPath();
        ctx.lineWidth = 3;
        ctx.moveTo(rectStart, MARGIN_TOP - 1.5);
        ctx.lineTo(rectEnd, MARGIN_TOP - 1.5);
        ctx.lineTo(rectEnd, MARGIN_TOP + RECT_HEIGHT + 1.5);
        ctx.lineTo(rectStart, MARGIN_TOP + RECT_HEIGHT + 1.5);
        ctx.lineTo(rectStart, MARGIN_TOP - 1.5);
        ctx.stroke();
        ctx.closePath();
      }
    }
  }

  onMouseClick({x}: {x: number}) {
    const data = this.data();
    if (data === undefined) return false;
    const {timeScale} = globals.frontendLocalState;
    const time = timeScale.pxToTime(x);
    const index = search(data.startNs, time);
    const ts = index === -1 ? undefined : data.startNs[index];
    const state = index === -1 ? undefined : data.strings[data.state[index]];
    const utid = this.config.utid;
    if (ts && state) {
      globals.dispatch(Actions.selectThreadState({utid, ts, state}));
      return true;
    }
    return false;
  }
}

trackRegistry.register(ThreadStateTrack);
