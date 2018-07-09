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

import {TrackCanvasContext} from './track_canvas_context';

// Creates virtual (width, height) canvas context backed by a
// real (width, height*2) canvas for the the purposes of implementing
// a (width, Inf) canvas with smooth scrolling.

export class CanvasController {
  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private tctx: TrackCanvasContext;

  private top = 0;
  private canvasHeight: number;

  // TODO: This should be removed, and canvasHeight used instead, once
  // conditional rendering of tracks has been figured out.
  private maxHeight = 100000;

  // Number of additionally rendered pixels above/below for compositor scrolling
  private extraHeightPerSide: number;

  constructor(private width: number, private height: number) {
    this.canvas = document.createElement('canvas');
    this.canvasHeight = this.height * 2;
    this.extraHeightPerSide = Math.round((this.canvasHeight - this.height) / 2);

    const dpr = window.devicePixelRatio;
    this.canvas.style.position = 'absolute';
    this.canvas.style.top = (-1 * this.extraHeightPerSide).toString() + 'px';
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
    this.tctx = new TrackCanvasContext(this.ctx, {
      left: 0,
      top: this.extraHeightPerSide,
      width: this.width,
      height: this.maxHeight
    });
  }

  clear() {
    this.tctx.fillStyle = 'white';
    this.tctx.fillRect(0, 0, this.width, this.maxHeight);
  }

  getContext() {
    return this.tctx;
  }

  getCanvasElement() {
    return this.canvas;
  }

  updateScrollOffset(scrollOffset: number) {
    this.top = scrollOffset + this.extraHeightPerSide;
    this.tctx.setYOffset(scrollOffset * -1);
  }

  getCanvasScrollOffset() {
    return this.top;
  }
}
