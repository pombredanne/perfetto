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

import {CanvasController} from './canvas_controller';
import {BoundingRect, VirtualCanvasContext} from './virtual_canvas_context';

export class RootVirtualContext extends VirtualCanvasContext {
  private cc: CanvasController;
  protected ctx: CanvasRenderingContext2D;

  constructor(cc: CanvasController) {
    const context = cc.getCanvasElement().getContext('2d');
    if (!context) {
      throw new Error('Could not create Canvas Context');
    }

    super(context);

    this.cc = cc;
    this.ctx = context;
  }

  isOnCanvas() {
    return this.checkRectOnCanvas(this.getBoundingRect());
  }

  checkRectOnCanvas(boundingRect: BoundingRect) {
    const canvasTop = this.cc.getCanvasTopOffset();
    const canvasBottom = canvasTop + this.cc.height;
    const rectBottom = boundingRect.top + boundingRect.height;
    const rectRight = boundingRect.left + boundingRect.width;

    const heightIntersects =
        boundingRect.top <= canvasBottom && rectBottom >= canvasTop;
    const widthIntersects =
        boundingRect.left <= this.cc.width && rectRight >= 0;

    return heightIntersects && widthIntersects;
  }

  getBoundingRect() {
    return {
      top: this.cc.getCanvasTopOffset() * -1,
      left: 0,
      width: Infinity,
      height: Infinity
    };
  }
}
