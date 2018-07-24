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

import {Milliseconds, TimeScale} from './time_scale';
import {VirtualCanvasContext} from './virtual_canvas_context';

export abstract class GridlineHelper {
  static drawGridLines(
      ctx: VirtualCanvasContext, x: TimeScale,
      timeBounds: [Milliseconds, Milliseconds], width: number,
      height: number): void {
    const range = timeBounds[1] - timeBounds[0];
    const desiredSteps = width / 80;
    const step = GridlineHelper.getStepSize(range, desiredSteps);
    const start = Math.round(timeBounds[0] / step) * step;

    ctx.strokeStyle = '#999999';
    ctx.lineWidth = 1;

    for (let t: Milliseconds = start; t < timeBounds[1]; t += step) {
      const xPos = Math.floor(x.msToPx(t)) + 0.5;

      if (xPos <= width) {
        ctx.beginPath();
        ctx.moveTo(xPos, 0);
        ctx.lineTo(xPos, height);
        ctx.stroke();
      }
    }
  }

  /**
   * Calculates a step size of gridlines for a given time range such that there
   * are between 10 and 25 steps. The only possible step sizes are 2, 5, or 10,
   * except for factors of 10 for scaling.
   */
  static getStepSize(range: Milliseconds, desiredSteps = 15): Milliseconds {
    const zeros = Math.floor(Math.log10(range));
    let step = Math.pow(10, zeros);
    const distToDesired = (step: number) =>
        Math.abs(range / step - desiredSteps);

    if (distToDesired(step / 10) < distToDesired(step)) {
      step /= 10;
    }
    if (distToDesired(step / 5) < distToDesired(step)) {
      step /= 5;
    }
    if (distToDesired(step / 2) < distToDesired(step)) {
      step /= 2;
    }
    return step;
  }
}