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

import {TrackCanvasContext} from "./track_canvas_context";

export class CanvasController {

  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private cctx: TrackCanvasContext;

  private top = 0;
  private canvasHeight = 0;

  // The top level context should not worry about clipping
  private maxHeight = 100000;

  // Number of additionally rendered pixels above/below for compositor scrolling
  private extraHeightPerSide = 0;

  constructor(private width = 1000, private height = 600) {
    this.canvas = document.createElement('canvas');

    const ctx = this.canvas.getContext('2d');

    if(!ctx) {
      throw new Error('Canvas Context not found');
    }

    this.ctx = ctx;
    this.cctx = new TrackCanvasContext(this.ctx, {
      left: 0,
      top: this.extraHeightPerSide,
      width: this.width,
      height: this.maxHeight
    });
  }

  setDimensions(width: number, height: number) {
    this.width = width;
    this.height = height;

    this.canvasHeight = this.height * 2;
    this.extraHeightPerSide = Math.round((this.canvasHeight - this.height) / 2);

    const dpr = window.devicePixelRatio;
    this.canvas.style.position = 'absolute';
    this.canvas.style.top = (-1 * this.extraHeightPerSide).toString() + 'px';
    this.canvas.style.width = this.width.toString() + 'px';
    this.canvas.style.height = this.canvasHeight.toString() + 'px';
    this.canvas.width = this.width * dpr;
    this.canvas.height = this.canvasHeight * dpr;

    this.ctx.scale(dpr, dpr);
  }

  clear() {
    this.cctx.fillStyle = 'white';
    this.cctx.fillRect(0, 0, this.width, this.maxHeight);
  }

  getContext() {
    return this.cctx;
  }

  getCanvasElement() {
    return this.canvas;
  }

  updateScrollOffset(scrollOffset: number) {
    this.top = scrollOffset + this.extraHeightPerSide;
  }

  getCanvasScrollOffset() {
    return this.top;
  }
}
