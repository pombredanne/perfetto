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
import {trackRegistry} from '../../frontend/track_registry';
import {VirtualCanvasContext} from '../../frontend/virtual_canvas_context';

class CpuSliceTrack extends TrackImpl {
  static readonly type = 'CpuSliceTrack';
  static create(trackState: TrackState): CpuSliceTrack {
    return new CpuSliceTrack(trackState);
  }

  constructor(trackState: TrackState) {
    super(trackState);
  }

  draw(vCtx: VirtualCanvasContext, width: number): void {
    vCtx.fillStyle = '#ccc';
    vCtx.fillRect(0, 0, width, this.trackState.height);
    vCtx.font = '16px Arial';
    vCtx.fillStyle = '#000';
    vCtx.fillText('Drawing ' + CpuSliceTrack.name, width / 2, 20);
  }
}

trackRegistry.register(CpuSliceTrack);