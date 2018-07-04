/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import {TrackCanvasContext} from "./track_canvas_context";

export class CanvasController {

  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private cctx: TrackCanvasContext;

  private top = 0;
  private canvasHeight: number;

  constructor(private width: number, private height: number) {

    this.canvas = document.createElement('canvas');
    this.canvasHeight = this.height * 2;

    this.canvas.style.position = 'absolute';
    this.canvas.style.top = '-' + (this.canvasHeight - this.height) / 2 + 'px';
    this.canvas.style.left = '0';
    this.canvas.style.width = this.width + 'px';
    this.canvas.style.height = this.canvasHeight + 'px';
    this.canvas.width = this.width;
    this.canvas.height = this.canvasHeight;

    const ctx = this.canvas.getContext('2d');

    if(!ctx) {
      throw new Error('Canvas Context not found');
    }

    this.ctx = ctx;
    this.cctx = new TrackCanvasContext(this.ctx, {
      left: 0,
      top: this.top,
      width: this.width,
      height: this.canvasHeight
    });
    console.log('ya');
  }

  getContext() {
    return this.cctx;
  }

  getCanvasElement() {
    return this.canvas;
  }

  updateScrollOffset(scrollOffset: number) {
    this.top = scrollOffset;
  }

  getCanvasScrollOffset() {
    return this.top;
  }
}