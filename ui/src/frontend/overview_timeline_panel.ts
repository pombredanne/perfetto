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

import {TimeSpan} from '../common/time';

import {DragGestureHandler} from './drag_gesture_handler';
import {globals} from './globals';
import {Panel} from './panel';
import {TimeScale} from './time_scale';

export class OverviewTimelinePanel implements Panel {
  private width? : number;
  private dragStartPx = 0;
  private gesture?: DragGestureHandler;
  private timeScale?: TimeScale;
  private totTime = new TimeSpan(0,0);

  constructor() {}

  getHeight(): number {
    return 100;
  }

  updateDom(dom: HTMLElement) {
    this.width = dom.getBoundingClientRect().width;
    this.totTime = new TimeSpan(
        0, globals.state.traceTime.endSec - globals.state.traceTime.startSec);
    this.timeScale = new TimeScale(this.totTime, [0, this.width]);

    if (this.gesture === undefined) {
      this.gesture = new DragGestureHandler(
          dom as HTMLElement,
          this.onDrag.bind(this),
          this.onDragStart.bind(this),
          this.onDragEnd.bind(this));
    }
  }

  renderCanvas(ctx: CanvasRenderingContext2D) {
    if (this.width === undefined) return;
    if (this.timeScale === undefined) return;
    // Draw visible time.
    const vizTime = globals.frontendLocalState.visibleWindowTime;
    // console.log('viz: ', vizTime.start.toString(), vizTime.end.toString());

    const vizStartPx = this.timeScale.timeToPx(vizTime.start);
    const vizEndPx = this.timeScale.timeToPx(vizTime.end);
    // console.log('w: ', this.timeScale!.getPxBounds(), vizStartPx, vizEndPx);

    ctx.fillStyle = '#eee';
    ctx.fillRect(0, 0, vizStartPx, this.getHeight());
    ctx.fillRect(vizEndPx, 0, this.width - vizEndPx, this.getHeight());

    // Draw brushes.
    ctx.fillStyle = '#999';
    ctx.fillRect(vizStartPx, 0, 1, this.getHeight());
    ctx.fillRect(vizEndPx, 0, 1, this.getHeight());
    const HANDLE_WIDTH = 3;
    const HANDLE_HEIGHT = 30;
    const y = (this.getHeight() - HANDLE_HEIGHT) / 2;
    ctx.fillRect(vizStartPx - HANDLE_WIDTH, y, HANDLE_WIDTH, HANDLE_HEIGHT);
    ctx.fillRect(vizEndPx + 1, y, HANDLE_WIDTH, HANDLE_HEIGHT);
  }

  onDrag(x: number) {
    // Set visible time limits from selection.
    if (this.timeScale === undefined) return;
    let tStart = this.timeScale.pxToTime(this.dragStartPx);
    let tEnd = this.timeScale.pxToTime(x);
    if (tStart > tEnd) [tStart, tEnd] = [tEnd, tStart];
    const vizTime = new TimeSpan(tStart, tEnd);
    globals.frontendLocalState.updateVisibleTime(vizTime);
    globals.rafScheduler.scheduleOneRedraw();
  }

  onDragStart(x: number) {
    this.dragStartPx = x;
  }

  onDragEnd() {
    this.dragStartPx = 0;
  }
}