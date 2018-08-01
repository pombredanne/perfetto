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

export class Animation {
  private tStart = 0;
  private tEnd = 0;
  private tLastFrame = 0;
  private rafId = 0;

  constructor(private onAnimationStep: (timeSinceLastMs: number) => void) {}

  start(durationMs: number) {
    const nowMs = performance.now();
    if (nowMs <= this.tEnd) {
      return;  // Another animation is in progress.
    }
    this.tLastFrame = 0;
    this.tStart = nowMs;
    this.tEnd = this.tStart + durationMs;
    this.rafId = requestAnimationFrame(this.onAnimationFrame.bind(this));
  }

  stop() {
    this.tStart = this.tEnd = 0;
    cancelAnimationFrame(this.rafId);
  }

  get startTimeMs(): number {
    return this.tStart;
  }

  private onAnimationFrame(nowMs: number) {
    if (nowMs < this.tEnd) {
      this.rafId = requestAnimationFrame(this.onAnimationFrame.bind(this));
    }
    this.onAnimationStep(nowMs - (this.tLastFrame || nowMs));
    this.tLastFrame = nowMs;
  }
}
