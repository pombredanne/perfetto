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

import {TrackState} from '../../common/state';
import {TimeSpan} from '../../common/time';
import {globals} from '../../frontend/globals';
import {Track} from '../../frontend/track';
import {trackRegistry} from '../../frontend/track_registry';

import {CpuSlice, CpuSliceTrackData, TRACK_KIND} from './common';

const MARGIN_TOP = 20;
const RECT_HEIGHT = 30;

function sliceIsVisible(
    slice: {start: number, end: number}, visibleWindowTime: TimeSpan) {
  return slice.end > visibleWindowTime.start &&
      slice.start < visibleWindowTime.end;
}

function cropText(str:string, charWidth: number, rectWidth: number) {
  const maxTextWidth = rectWidth - 15;
  let displayText = '';
  const nameLength = str.length * charWidth;
  if (nameLength < maxTextWidth) {
    displayText = str;
  } else {
    // -3 for the 3 ellipsis.
    const displayedChars = Math.floor(maxTextWidth / charWidth) - 3;
    if (displayedChars > 3) {
      displayText = str.substring(0, displayedChars) + '...';
    }
  }
  return displayText;
}

class CpuSliceTrack extends Track {
  static readonly kind = TRACK_KIND;
  static create(trackState: TrackState): CpuSliceTrack {
    return new CpuSliceTrack(trackState);
  }

  private trackData: CpuSliceTrackData|undefined;
  private hoveredSlice: CpuSlice|null = null;

  constructor(trackState: TrackState) {
    super(trackState);
  }

  consumeData(trackData: CpuSliceTrackData) {
    this.trackData = trackData;
  }

  renderCanvas(ctx: CanvasRenderingContext2D): void {
    if (!this.trackData) return;
    const {timeScale, visibleWindowTime} = globals.frontendLocalState;
    ctx.textAlign = 'center';
    const charWidth = ctx.measureText('abcdefghij').width / 10;

    // TODO: this needs to be kept in sync with the hue generation algorithm
    // of overview_timeline_panel.ts
    let hue = (128 + (32 * this.trackState.cpu)) % 256;

    for (const slice of this.trackData.slices) {
      if (!sliceIsVisible(slice, visibleWindowTime)) continue;
      const rectStart = timeScale.timeToPx(slice.start);
      const rectEnd = timeScale.timeToPx(slice.end);
      const rectWidth = rectEnd - rectStart;
      if (rectWidth < 0.1) continue;

      ctx.fillStyle = `hsl(${hue}, 50%, ${slice === this.hoveredSlice ? 70 : 50}%`;
      ctx.fillRect(rectStart, MARGIN_TOP, rectEnd - rectStart, RECT_HEIGHT);

      // TODO: consider de-duplicating this code with the copied one from
      // chrome_slices/frontend.ts.
      let title = `[utid:${slice.utid}]`;
      let subTitle = '';
      const threadInfo = globals.threads.get(slice.utid);
      if (threadInfo !== undefined) {
        title = `${threadInfo.procName} [${threadInfo.pid}]`;
        subTitle = `${threadInfo.threadName} [${threadInfo.tid}]`;
      }
      title = cropText(title, charWidth, rectWidth);
      subTitle = cropText(subTitle, charWidth, rectWidth);
      const rectXCenter = rectStart + rectWidth / 2;
      ctx.fillStyle = '#fff';
      ctx.font = '12px Google Sans';
      ctx.fillText(title, rectXCenter, MARGIN_TOP + RECT_HEIGHT / 2 - 3);
      ctx.fillStyle = 'rgba(255, 255, 255, 0.6)';
      ctx.font = '10px Google Sans';
      ctx.fillText(subTitle, rectXCenter, MARGIN_TOP + RECT_HEIGHT / 2 + 11);
    }
  }

  onMouseMove({x, y}: {x: number, y: number}) {
    if (!this.trackData) return;
    const {timeScale} = globals.frontendLocalState;
    if (y < MARGIN_TOP || y > MARGIN_TOP + RECT_HEIGHT) {
      this.hoveredSlice = null;
      return;
    }
    const t = timeScale.pxToTime(x);
    this.hoveredSlice = null;

    for (const slice of this.trackData.slices) {
      if (slice.start <= t && slice.end >= t) {
        this.hoveredSlice = slice;
      }
    }
  }

  onMouseOut() {
    this.hoveredSlice = null;
  }
}

trackRegistry.register(CpuSliceTrack);
