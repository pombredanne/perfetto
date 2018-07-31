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
import {drawGridLines} from '../../frontend/gridline_helper';
import {TimeScale} from '../../frontend/time_scale';
import {Track} from '../../frontend/track';
import {TrackData} from '../../frontend/track';

interface CpuSlice {
  start: number;
  end: number;
  title: string;
}

export interface CpuSliceTrackData extends TrackData {
  id: string;
  trackKind: (typeof CpuSliceTrack)['kind'];
  // TODO: Is there any point of having this extra nesting?
  data: {slices: CpuSlice[];};
}

function isCpuSliceTrackData(trackData: TrackData):
    trackData is CpuSliceTrackData {
  return trackData.trackKind === CpuSliceTrack.kind;
}

function sliceIsVisible(
    slice: {start: number, end: number},
    visibleWindowMs: {start: number, end: number}) {
  return slice.end > visibleWindowMs.start && slice.start < visibleWindowMs.end;
}

class CpuSliceTrack extends Track {
  static readonly kind = TRACK_KIND;
  static create(trackState: TrackState): CpuSliceTrack {
    return new CpuSliceTrack(trackState);
  }

  private trackData: CpuSliceTrackData|undefined;

  constructor(trackState: TrackState) {
    super(trackState);
  }

  static validataData(trackData: TrackData): trackData is CpuSliceTrackData {
    return trackData.trackKind === 'CpuSliceTrackData';
  }

  setData(trackData: TrackData) {
    if (!isCpuSliceTrackData(trackData)) {
      throw Error('Wrong type assigned :(');
    }
    this.trackData = trackData;
  }

  renderCanvas(
      vCtx: VirtualCanvasContext, width: number, timeScale: TimeScale,
      visibleWindowMs: {start: number, end: number}): void {
    drawGridLines(
        vCtx,
        timeScale,
        [visibleWindowMs.start, visibleWindowMs.end],
        width,
        73);

    if (this.trackData) {
      for (const slice of this.trackData.data.slices) {
        if (!sliceIsVisible(slice, visibleWindowMs)) continue;
        const rectStart = timeScale.msToPx(slice.start);
        const rectEnd = timeScale.msToPx(slice.end);

        // TODO: Doing this for every slice is super ugly. Should we remove
        // bounds checking from virtual canvas context?
        const shownStart = Math.max(rectStart, 0);
        const shownEnd = Math.min(width, rectEnd);
        const shownWidth = shownEnd - shownStart;
        vCtx.fillStyle = '#c00';
        vCtx.fillRect(shownStart, 40, shownWidth, 30);
      }
    }
  }
}

trackRegistry.register(CpuSliceTrack);
import {trackRegistry} from '../../frontend/track_registry';
import {VirtualCanvasContext} from '../../frontend/virtual_canvas_context';

import {TRACK_KIND} from './common';
