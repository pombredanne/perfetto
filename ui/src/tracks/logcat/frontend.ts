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

import {Actions} from '../../common/actions';
import {TrackState} from '../../common/state';
import {checkerboardExcept} from '../../frontend/checkerboard';
import {globals} from '../../frontend/globals';
import {Track} from '../../frontend/track';
import {trackRegistry} from '../../frontend/track_registry';

import {Config, Data, LOGCAT_TRACK_KIND} from './common';

const MARGIN_TOP = 2;
const RECT_HEIGHT = 35;
const EVT_PX = 2;  // Width of an event in pixels.

const COLORS = [
  'hsl(122, 39%, 49%)',  // Green.
  'hsl(0, 0%, 70%)',     // Gray.
  'hsl(45, 100%, 51%)',  // Amber.
  'hsl(4, 90%, 58%)',    // Red.
  'hsl(291, 64%, 42%)',  // Purple.
];

const PRIO_TO_COLOR = [
  0,
  0,
  0,
  0,  // 0-3 (UNSPECIFIED, VERBOSE, DEBUG) -> Green.
  1,  // 4 (INFO) -> Gray.
  2,  // 5 (WARN) -> Amber.
  3,  // 6 (ERROR) -> Red.
  4,  // 7 (FATAL) -> Purple
];

function getCurResolution() {
  // Truncate the resolution to the closest power of 10.
  const resolution =
      globals.frontendLocalState.timeScale.deltaPxToDuration(EVT_PX);
  return Math.pow(10, Math.floor(Math.log10(resolution)));
}

class LogcatTrack extends Track<Config, Data> {
  static readonly kind = LOGCAT_TRACK_KIND;
  static create(trackState: TrackState): LogcatTrack {
    return new LogcatTrack(trackState);
  }

  private reqPending = false;

  constructor(trackState: TrackState) {
    super(trackState);
  }

  reqDataDeferred() {
    const {visibleWindowTime} = globals.frontendLocalState;
    const reqStart = visibleWindowTime.start - visibleWindowTime.duration;
    const reqEnd = visibleWindowTime.end + visibleWindowTime.duration;
    const reqRes = getCurResolution();
    this.reqPending = false;
    globals.dispatch(Actions.reqTrackData({
      trackId: this.trackState.id,
      start: reqStart,
      end: reqEnd,
      resolution: reqRes
    }));
  }

  renderCanvas(ctx: CanvasRenderingContext2D): void {
    const {timeScale, visibleWindowTime} = globals.frontendLocalState;

    const data = this.data();
    const inRange = data !== undefined &&
        (visibleWindowTime.start >= data.start &&
         visibleWindowTime.end <= data.end);
    if (!inRange || data === undefined ||
        data.resolution !== getCurResolution()) {
      if (!this.reqPending) {
        this.reqPending = true;
        setTimeout(() => this.reqDataDeferred(), 50);
      }
    }
    if (data === undefined) return;  // Can't possibly draw anything.

    const dataStartPx = timeScale.timeToPx(data.start);
    const dataEndPx = timeScale.timeToPx(data.end);
    const visibleStartPx = timeScale.timeToPx(visibleWindowTime.start);
    const visibleEndPx = timeScale.timeToPx(visibleWindowTime.end);

    checkerboardExcept(
        ctx, visibleStartPx, visibleEndPx, dataStartPx, dataEndPx);

    const quantWidth =
        Math.max(EVT_PX, timeScale.deltaTimeToPx(data.resolution));
    const BLOCK_H = RECT_HEIGHT / COLORS.length;
    for (let i = 0; i < data.timestamps.length; i++) {
      for (let prio = 0; prio < 8; prio++) {
        if ((data.severities[i] & (1 << prio)) === 0) continue;
        const lev = PRIO_TO_COLOR[prio];
        ctx.fillStyle = COLORS[lev];
        const px = Math.floor(timeScale.timeToPx(data.timestamps[i]));
        ctx.fillRect(px, MARGIN_TOP + BLOCK_H * lev, quantWidth, BLOCK_H);
      }
    }
  }
}

trackRegistry.register(LogcatTrack);
