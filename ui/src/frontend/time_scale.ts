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
 * Defines a mapping between pixels and nanoseconds for the entire application.
 */
export class GlobalTimeScale {
  constructor(
      private tStart: Nanoseconds, private tEnd: Nanoseconds,
      private pxStart: Pixels, private pxEnd: Pixels,
      private pxOffset: Pixels = 0) {}

  tsToPx(time: Nanoseconds): Pixels {
    const percentage: number = (time - this.tStart) / (this.tEnd - this.tStart);
    const percentagePx = percentage * (this.pxEnd - this.pxStart);

    return this.pxStart as number + percentagePx - this.pxOffset;
  }

  pxToTs(px: Pixels): Nanoseconds {
    const percentage = (px - this.pxStart) / (this.pxEnd - this.pxStart);
    return this.tStart as number + percentage * (this.tEnd - this.tStart);
  }

  relativePxToTs(px: Pixels): Nanoseconds {
    return this.pxToTs(px) - this.pxToTs(0);
  }

  setTimeLimits(tStart: Nanoseconds, tEnd: Nanoseconds) {
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

/**
 * Defines a mapping between pixels and nanoseconds given a different local
 * coordinate system caused by a smaller bounding rectangle such as a Track
 * content container, which defines an offset and a width.
 */
export class OffsetTimeScale {
  constructor(
      private scale: TimeScale, private pxOffset: Pixels = 0,
      private width: Pixels = 0) {}

  tsToPx(time: Nanoseconds): Pixels {
    const result = this.scale.tsToPx(time) - this.pxOffset;

    if (result < 0) return 0;
    if (result > this.width) return this.width;

    return result;
  }

  pxToTs(px: Pixels): Nanoseconds {
    return this.scale.pxToTs(px as number + (this.pxOffset as number));
  }

  relativePxToTs(px: Pixels): Nanoseconds {
    return this.scale.pxToTs(px as number + (this.pxOffset as number)) -
        this.scale.pxToTs(0);
  }

  getTimeLimits(): {start: Nanoseconds, end: Nanoseconds} {
    return this.scale.getTimeLimits();
  }

  setWidth(width: Pixels) {
    this.width = width;
  }

  getWidth(): Pixels {
    return this.width;
  }

  setOffset(pxOffset: Pixels) {
    this.pxOffset = pxOffset;
  }
}

// We are using enums because TypeScript does proper type checking for those,
// and disallows assigning a pixel value to a milliseconds value, even though
// they are numbers. Using types, this safeguard would not be here.
// See: https://stackoverflow.com/a/43832165

export enum Pixels {
}
export enum Nanoseconds {
}
export type TimeScale = GlobalTimeScale | OffsetTimeScale;