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

import {TrackImpl} from '../../frontend/track';
// import { TrackState } from '../../common/state';
import {VirtualCanvasContext} from '../../frontend/virtual_canvas_context';

// Dummy implementation as an exmaple of a second type of track.
export class CpuCounterTrack extends TrackImpl {
  static type = 'CpuCounterTrack';

  draw(vCtx: VirtualCanvasContext): void {
    if (vCtx.isOnCanvas()) {
      vCtx.fillStyle = '#eee';
      vCtx.fillRect(0, 0, this.width, this.trackState.height);
      vCtx.font = '16px Arial';
      vCtx.fillStyle = '#000';
      vCtx.fillText(
          'Drawing ' + CpuCounterTrack.type, Math.round(this.width / 2), 20);
    }
  }
}
