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

import {Nanoseconds} from '../../frontend/time_scale';
import {TrackCanvasContent} from '../../frontend/track_canvas_content';
import {VirtualCanvasContext} from '../../frontend/virtual_canvas_context';

export class CpuSlicesTrackCanvasContent extends TrackCanvasContent {
  render(ctx: VirtualCanvasContext, data: {trackName: string}) {
    ctx.fillStyle = '#eee';
    ctx.fillRect(0, 0, this.x.getWidth(), 73);

    this.drawGridLines(ctx, 73);

    const sliceStart: Nanoseconds = 100000;
    const sliceEnd: Nanoseconds = 400000;

    const rectStart = this.x.tsToPx(sliceStart);
    const rectWidth = this.x.tsToPx(sliceEnd) - rectStart;

    ctx.fillStyle = '#c00';
    ctx.fillRect(rectStart, 40, rectWidth, 30);

    ctx.font = '16px Arial';
    ctx.fillStyle = '#000';
    ctx.fillText(data.trackName + ' Canvas content', rectStart, 60);
  }
}
