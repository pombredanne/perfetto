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
 * Raf scheduler intended to use for higher frequency drawing, like canvas
 * updates.
 */
class LightRedrawer {
  private redrawCallbacks: Set<(() => void)>;
  private scheduledRedrawNextFrame = false;
  private currentlyDrawing = false;

  constructor() {
    this.redrawCallbacks = new Set<() => void>();
  }

  addCallback(cb: (() => void)): void {
    this.redrawCallbacks.add(cb);
  }

  removeCallback(cb: (() => void)): void {
    this.redrawCallbacks.delete(cb);
  }

  scheduleRedraw() {
    // There should be no good reason to schedule redraw while redrawing.
    if (this.currentlyDrawing) {
      throw Error('Cannot schedule redraw while drawing.');
    }

    if (this.scheduledRedrawNextFrame) return;
    this.scheduledRedrawNextFrame = true;

    window.requestAnimationFrame(() => {
      this.currentlyDrawing = true;
      for (const cb of this.redrawCallbacks) {
        cb();
      }
      this.scheduledRedrawNextFrame = false;
      this.currentlyDrawing = false;
    });
  }

  clearCallbacks() {
    this.redrawCallbacks.clear();
  }
}

export const lightRedrawer = new LightRedrawer();