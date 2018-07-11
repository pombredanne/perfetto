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

/**
 * RootVirtualContext is a VirtualCanvasContext that has knowledge of the
 * actual canvas element and the scroll position, which it can use to determine
 * whether any rect is within the canvas. ChildVirtualContexts can use this to
 * determine whether they should execute their draw calls.
 */
export class RootVirtualContext extends VirtualCanvasContext {
  constructor(private canvasController: CanvasController) {
    super(canvasController.getCanvasContext());
  }

  isOnCanvas() {
    return this.checkRectOnCanvas(this.getBoundingRect());
  }

  checkRectOnCanvas(boundingRect: BoundingRect) {
    const canvasTop = this.canvasController.getCanvasTopOffset();
    const canvasBottom = canvasTop + this.canvasController.canvasHeight;
    const rectBottom = boundingRect.y + boundingRect.height;
    const rectRight = boundingRect.x + boundingRect.width;

    const heightIntersects =
        boundingRect.y <= canvasBottom && rectBottom >= canvasTop;
    const widthIntersects =
        boundingRect.x <= this.canvasController.canvasWidth && rectRight >= 0;

    return heightIntersects && widthIntersects;
  }

  /**
   * This defines a BoundingRect that causes correct positioning of the context
   * contents due to the scroll position, without causing bounds checking.
   */
  getBoundingRect(): BoundingRect {
    return {
      // As the user scrolls down, the contents have to move up.
      y: this.canvasController.getCanvasTopOffset() * -1,
      x: 0,
      width: Infinity,
      height: Infinity
    };
  }
}
