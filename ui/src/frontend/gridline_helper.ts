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

const DESIRED_PX_PER_STEP = 80;

export function drawGridLines(
    ctx: VirtualCanvasContext,
    x: TimeScale,
    timeBounds: [Milliseconds, Milliseconds],
    width: number,
    height: number): void {
  const range = timeBounds[1] - timeBounds[0];
  const desiredSteps = width / DESIRED_PX_PER_STEP;
  const step = getGridStepSize(range, desiredSteps);
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
 * Returns the step size of a grid line. The returned step size has two
 * properties:
 *
 * (1) It is 1, 2, or 5, multipled by some integer power of 10.
 * (2) It is as close to possible to |desiredSteps|.
 */
export function getGridStepSize(
    range: Milliseconds, desiredSteps: number): Milliseconds {
  // First, get the largest possible power of 10 that is smaller than the
  // desired step size, and set it to the current step size.
  // For example, if the range is 2345ms and the desired steps is 10, then the
  // desired step size is 234.5 and the step size will be set to 100.
  const desiredStepSize = range / desiredSteps;
  const zeros = Math.floor(Math.log10(desiredStepSize));
  let stepSize = Math.pow(10, zeros);

  // This function first calculates how many steps within the range a certain
  // stepSize will produce, and returns the difference between that and
  // desiredSteps.
  const distToDesired = (evaluatedStepSize: number) =>
      Math.abs(range / evaluatedStepSize - desiredSteps);

  // We now check if we can lower distToDesired(stepSize) by increasing
  // stepSize. Since we only want step sizes that are 1, 2, or 5 times some
  // power of 10, we can only multiply by 5 and 2. We try these factors in
  // descending order. Note that we only need make the step size bigger, not
  // smaller, to get to the desired stepSize, since the power of 10 obtained
  // above is never smaller than the desired step size. Also note that we can
  // only multiply the initial stepSize by up to 10 since we are already close
  // to the desired number of steps.
  if (distToDesired(stepSize * 5) < distToDesired(stepSize) &&
      distToDesired(stepSize * 5) < distToDesired(stepSize * 2)) {
    stepSize *= 5;
  }
  if (distToDesired(stepSize * 2) < distToDesired(stepSize)) {
    stepSize *= 2;
  }
  return stepSize;
}