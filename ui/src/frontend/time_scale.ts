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

/**
 * Defines a mapping between pixels and Milliseconds for the entire application.
 * Scales times from tStart to tEnd to pixel values pxStart to pxEnd.
 */
export class TimeScale {
  private tStart: Milliseconds;
  private tEnd: Milliseconds;
  private pxStart: Pixels;
  private pxEnd: Pixels;

  constructor(timeBounds: [number, number], pxBounds: [number, number]) {
    this.tStart = timeBounds[0];
    this.tEnd = timeBounds[1];
    this.pxStart = pxBounds[0];
    this.pxEnd = pxBounds[1];
  }

  tsToPx(time: Milliseconds): Pixels {
    const percentage: number = (time - this.tStart) / (this.tEnd - this.tStart);
    const percentagePx = percentage * (this.pxEnd - this.pxStart);

    return this.pxStart as number + percentagePx;
  }

  pxToTs(px: Pixels): Milliseconds {
    const percentage = (px - this.pxStart) / (this.pxEnd - this.pxStart);
    return this.tStart as number + percentage * (this.tEnd - this.tStart);
  }

  relativePxToTs(px: Pixels): Milliseconds {
    return px * (this.tEnd - this.tStart) / (this.pxEnd - this.pxStart);
  }

  setTimeLimits(tStart: Milliseconds, tEnd: Milliseconds) {
    this.tStart = tStart;
    this.tEnd = tEnd;
  }

  setPxLimits(pxStart: Pixels, pxEnd: Pixels) {
    this.pxStart = pxStart;
    this.pxEnd = pxEnd;
  }

  getTimeLimits() {
    return {start: this.tStart, end: this.tEnd};
  }
}

// We are using enums because TypeScript does proper type checking for those,
// and disallows assigning a pixel value to a milliseconds value, even though
// they are numbers. Using types, this safeguard would not be here.
// See: https://stackoverflow.com/a/43832165

export enum Pixels {
}
export enum Milliseconds {
}
