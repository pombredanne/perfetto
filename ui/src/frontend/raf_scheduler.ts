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

export type ActionCallback = (nowMs: number) => void;
export type RedrawCallback = (nowMs: number) => void;

// This class orchestrates all RAFs in the UI. It ensures that there is only
// one animation frame handler overall and that callbacks are called in
// predictable order. There are two types of callbacks here:
// - actions (e.g. pan/zoon animations), which will alter the "fast"
//  (main-thread-only) state (e.g. update visible time bounds @ 60 fps).
// - redraw callbacks that will repaint canvases.
// This class guarantees that, on each frame, redraw callbacks are called after
// all action callbacks.
export class RafScheduler {
  private actionCallbacks = new Set<ActionCallback>();
  private canvasRedrawCallbacks = new Set<RedrawCallback>();
  private domRedrawCallback: RedrawCallback|undefined;
  private hasScheduledNextFrame = false;
  private requestedCanvasRedraw = false;
  private requestedFullRedraw = false;
  private isRedrawing = false;

  start(cb: ActionCallback) {
    this.actionCallbacks.add(cb);
    this.maybeScheduleAnimationFrame();
  }

  stop(cb: ActionCallback) {
    this.actionCallbacks.delete(cb);
  }

  addCanvasRedrawCallback(cb: RedrawCallback) {
    this.canvasRedrawCallbacks.add(cb);
  }

  removeCanvasRedrawCallback(cb: RedrawCallback) {
    this.canvasRedrawCallbacks.delete(cb);
  }

  scheduleCanvasRedraw() {
    this.requestedCanvasRedraw = true;
    this.maybeScheduleAnimationFrame();
  }

  setDomRedrawCallback(cb: RedrawCallback) {
    this.domRedrawCallback = cb;
  }

  unsetDomRedrawCallback() {
    this.domRedrawCallback = undefined;
  }

  scheduleFullRedraw() {
    this.requestedFullRedraw = true;
    this.maybeScheduleAnimationFrame();
  }

  canvasRedraw(nowMs: number) {
    if (this.isRedrawing) return;
    this.isRedrawing = true;
    for (const redraw of this.canvasRedrawCallbacks) redraw(nowMs);
    this.isRedrawing = false;
  }

  private maybeScheduleAnimationFrame() {
    if (this.hasScheduledNextFrame) return;
    if (this.actionCallbacks.size !== 0 || this.requestedCanvasRedraw ||
        this.requestedFullRedraw) {
      this.hasScheduledNextFrame = true;
      window.requestAnimationFrame(this.onAnimationFrame.bind(this));
    }
  }

  private onAnimationFrame(nowMs: number) {
    this.hasScheduledNextFrame = false;
    this.requestedCanvasRedraw = false;

    const doFullRedraw = this.requestedFullRedraw;
    this.requestedFullRedraw = false;

    for (const action of this.actionCallbacks) action(nowMs);
    if (doFullRedraw && this.domRedrawCallback) {
      this.domRedrawCallback(nowMs);
    }
    this.canvasRedraw(nowMs);

    this.maybeScheduleAnimationFrame();
  }
}
