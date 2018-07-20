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
import {TrackImpl} from '../../frontend/track_impl';
import {VirtualCanvasContext} from '../../frontend/virtual_canvas_context';

export class CpuSliceTrack extends TrackImpl {
  static readonly type = 'CpuSliceTrack';
  static create(trackState: TrackState, width: number): CpuSliceTrack {
    return new CpuSliceTrack(trackState, width);
  }

  constructor(trackState: TrackState, width: number) {
    super(trackState, width);
  }

  draw(vCtx: VirtualCanvasContext): void {
    if (vCtx.isOnCanvas()) {
      vCtx.fillStyle = '#ccc';
      vCtx.fillRect(0, 0, this.width, this.trackState.height);
      vCtx.font = '16px Arial';
      vCtx.fillStyle = '#000';
      vCtx.fillText('Drawing ' + CpuSliceTrack.name, this.width / 2, 20);
    }
  }
}
