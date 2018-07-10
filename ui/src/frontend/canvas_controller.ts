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

import {RootCanvasContext} from './root_canvas_context';

const CANVAS_OVERDRAW_FACTOR = 2;

/**
 * Creates a canvas with a context that is set up of compositor scrolling.
 * Creates virtual (width, height) canvas context backed by a  real
 * (width, height*2) canvas for the the purposes of implementing a (width, Inf)
 * canvas with smooth scrolling.
 */
export class CanvasController {
  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private rootTrackContext: RootCanvasContext;

  private scrollOffset = 0;
  private canvasHeight: number;

  // Number of additionally rendered pixels above/below for compositor scrolling
  private extraHeightPerSide: number;

  constructor(private width: number, private height: number) {
    this.canvas = document.createElement('canvas');

    this.canvasHeight = this.height * CANVAS_OVERDRAW_FACTOR;
    this.extraHeightPerSide = Math.round((this.canvasHeight - this.height) / 2);

    const dpr = window.devicePixelRatio;
    this.canvas.style.width = this.width.toString() + 'px';
    this.canvas.style.height = this.canvasHeight.toString() + 'px';
    this.canvas.width = this.width * dpr;
    this.canvas.height = this.canvasHeight * dpr;

    const ctx = this.canvas.getContext('2d');

    if (!ctx) {
      throw new Error('Could not create canvas context');
    }

    ctx.scale(dpr, dpr);

    this.ctx = ctx;
    this.rootTrackContext = new RootCanvasContext(
        this.ctx,
        {
          left: 0,
          top: this.extraHeightPerSide,
          width: this.width,
          height: Number.MAX_SAFE_INTEGER  // The top context should not clip.,
        },
        this.canvasHeight);
  }

  clear(): void {
    this.ctx.fillStyle = 'white';
    this.ctx.fillRect(0, 0, this.width, this.canvasHeight);
  }

  getContext(): RootCanvasContext {
    return this.rootTrackContext;
  }

  getCanvasElement(): HTMLCanvasElement {
    return this.canvas;
  }

  /**
   * Places the canvas and its contents at the correct position.
   * Re-centers the canvas element in the current viewport, and sets the context
   * offsets such that the contents move up as we scroll, while rendering the
   * first track within the viewport.
   */
  updateScrollOffset(scrollOffset: number): void {
    this.scrollOffset = scrollOffset;
    this.rootTrackContext.setYOffset(-1 * this.getCanvasTopOffset());
  }

  getCanvasTopOffset(): number {
    return this.scrollOffset - this.extraHeightPerSide;
  }
}
