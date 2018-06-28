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

export class TrackCanvasContext {

  constructor(private ctx: CanvasRenderingContext2D | TrackCanvasContext,
              private rect: {
                left: number,
                top: number,
                width: number,
                height: number
              }) {}

  fillRect(x: number, y: number, width: number, height: number) {

    if(x < 0 || x + width > this.rect.width ||
      y < 0 || y + height > this.rect.height) {
      /*throw new OutOfBoundsDrawingError('Rect out of bounds ' +
        this.rect.width + ', ' + this.rect.height + ': topleft ' + x + ', ' + y
        + ', bottom right: ' + (x + width) + ', ' + (y + height));*/
    }

    this.ctx.fillRect(x + this.rect.left, y + this.rect.top, width, height);
  }

  setDimensions(width: number, height: number)
  {
    this.rect.width = width;
    this.rect.height = height;
  }

  setYOffset(offset: number) {
    this.rect.top = offset;
  }

  moveTo(x: number, y: number) {
    this.ctx.moveTo(x + this.rect.left, y + this.rect.top);
  }

  lineTo(x: number, y: number) {
    this.ctx.lineTo(x + this.rect.left, y + this.rect.top);
  }

  stroke() {
    this.ctx.stroke();
  }

  beginPath() {
    this.ctx.beginPath();
  }

  closePath() {
    this.ctx.closePath();
  }

  measureText(text: string): TextMetrics {
    return this.ctx.measureText(text);
  }

  fillText(text: string, x: number, y: number) {
    this.ctx.fillText(text, x + this.rect.left, y + this.rect.top);
  }

  set strokeStyle(v: string) {
    this.ctx.strokeStyle = v;
  }

  set fillStyle(v: string) {
    this.ctx.fillStyle = v;
  }

  set lineWidth(width: number) {
    this.ctx.lineWidth = width;
  }

  set font(fontString: string) {
    this.ctx.font = fontString;
  }
}

export class OutOfBoundsDrawingError extends Error {

}
