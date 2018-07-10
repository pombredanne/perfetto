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

import {VirtualCanvasContext} from './virtual_canvas_context';

export class RootCanvasContext extends VirtualCanvasContext {
  constructor(
      protected ctx: CanvasRenderingContext2D|VirtualCanvasContext,
      protected rect:
          {left: number, top: number, width: number, height: number},
      private canvasHeight: number) {
    super(ctx, rect);
  }

  isOnCanvas(rect: {left: number, top: number, width: number, height: number}):
      boolean {
    const topPos = -1 * this.rect.top;
    const botPos = topPos + this.canvasHeight;

    return rect.top >= topPos && rect.top + rect.height <= botPos;
  }

  private pointIsOnCanvas(x: number, y: number) {
    const topPos = -1 * this.rect.top;
    const botPos = topPos + this.canvasHeight;
    return x >= 0 && x <= this.rect.width && y >= topPos && y <= botPos;
  }

  moveTo(x: number, y: number) {
    if (!this.pointIsOnCanvas(x, y)) {
      throw new NotOnCanvasError(
          'moveto', {x, y}, this.rect, this.canvasHeight);
    }

    this.ctx.moveTo(x + this.rect.left, y + this.rect.top);
  }

  lineTo(x: number, y: number) {
    if (!this.pointIsOnCanvas(x, y)) {
      throw new NotOnCanvasError(
          'lineto', {x, y}, this.rect, this.canvasHeight);
    }

    this.ctx.lineTo(x + this.rect.left, y + this.rect.top);
  }

  fillText(text: string, x: number, y: number) {
    if (!this.pointIsOnCanvas(x, y)) {
      throw new NotOnCanvasError(
          'fill text', {x, y}, this.rect, this.canvasHeight);
    }
    this.ctx.fillText(text, x + this.rect.left, y + this.rect.top);
  }
}

export class NotOnCanvasError extends Error {
  constructor(
      action: string, drawing: {},
      bounds: {left: number, top: number, width: number, height: number},
      canvasHeight: number) {
    super(
        `Attempted to ${action} (${JSON.stringify(drawing)}) ` +
        `outside of canvas bounds ${JSON.stringify({
                                      left: bounds.left,
                                      top: bounds.top * -1,
                                      width: bounds.width,
                                      height: canvasHeight
                                    })}. ` +
        `Did you check trackContext.isOnCanvas()?`);
  }
}