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

import {Nanoseconds, OffsetTimeScale} from './time_scale';
import {VirtualCanvasContext} from './virtual_canvas_context';

export abstract class TrackCanvasContent {
  constructor(protected x: OffsetTimeScale) {}

  abstract render(ctx: VirtualCanvasContext, data?: {}): void;

  protected drawGridLines(ctx: VirtualCanvasContext, height: number): void {
    ctx.strokeStyle = '#999999';
    ctx.lineWidth = 1;

    const limits = this.x.getTimeLimits();
    const range = limits.end - limits.start;
    let step = 0.001;
    while (range / step > 20) {
      step *= 10;
    }
    if (range / step < 5) {
      step /= 5;
    }
    if (range / step < 10) {
      step /= 2;
    }

    const start = Math.round(limits.start / step) * step;

    for (let t: Nanoseconds = start; t < limits.end - step; t += step) {
      const xPos = Math.floor(this.x.tsToPx(t)) + 0.5;

      ctx.beginPath();
      ctx.moveTo(xPos, 0);
      ctx.lineTo(xPos, height);
      ctx.stroke();
    }
  }
}