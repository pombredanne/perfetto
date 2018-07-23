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


export class ZoomContent {
  static SCROLL_SPEED = 1;
  static ZOOM_IN_PERCENTAGE_SPEED = 0.95;
  static ZOOM_OUT_PERCENTAGE_SPEED = 1.05;
  static KEYBOARD_PAN_PX_PER_FRAME = 20;

  protected mouseDownX = -1;
  private mouseXpos = 0;

  constructor(
      private element: HTMLElement, private contentOffsetX: number,
      private onPanned: (movedPx: number) => void,
      private onZoomed:
          (zoomPositionPx: number, zoomPercentage: number) => void) {}

  init() {
    this.attachMouseEventListeners();
    this.handleKeyPanning();
    this.handleKeyZooming();
  }

  private handleKeyPanning() {
    document.body.addEventListener('keydown', (e) => {
      if (e.repeat) {
        return;
      }
      if (e.key === 'a') {
        startPan(true);
      } else if (e.key === 'd') {
        startPan(false);
      }
    }, {passive: true});
    document.body.addEventListener('keyup', (e) => {
      if (e.repeat) {
        return;
      }
      if (e.key === 'a' || e.key === 'd') {
        endPan();
      }
    });

    let panning = false;
    let panLeftFactor = 0;

    const pan = () => {
      this.onPanned(panLeftFactor * ZoomContent.KEYBOARD_PAN_PX_PER_FRAME);
      if (panning) {
        requestAnimationFrame(() => pan());
      }
    };

    const startPan = (left: boolean) => {
      panLeftFactor = left ? -1 : 1;
      if (panning) {
        return;
      }
      panning = true;
      pan();
    };
    const endPan = () => {
      panning = false;
    };
  }

  private handleKeyZooming() {
    document.body.addEventListener('keydown', (e) => {
      if (e.key === 'w') {
        startZoom(true);
      } else if (e.key === 's') {
        startZoom(false);
      }
    });
    document.body.addEventListener('keyup', (e) => {
      if (e.key === 'w' || e.key === 's') {
        endZoom();
      }
    });

    let zooming = false;

    const zoom = (zoomIn: boolean) => {
      const percentage = zoomIn ? ZoomContent.ZOOM_IN_PERCENTAGE_SPEED :
                                  ZoomContent.ZOOM_OUT_PERCENTAGE_SPEED;
      this.onZoomed(this.mouseXpos, percentage);

      if (zooming) {
        requestAnimationFrame(() => zoom(zoomIn));
      }
    };

    const startZoom = (zoomIn: boolean) => {
      if (zooming) {
        return;
      }
      zooming = true;
      zoom(zoomIn);
    };
    const endZoom = () => {
      zooming = false;
    };
  }

  private attachMouseEventListeners() {
    this.element.addEventListener('mousedown', (e) => this.onMouseDown(e));
    this.element.addEventListener('mousemove', (e) => this.onMouseMove(e));
    this.element.addEventListener('mouseup', () => this.onMouseUp());
    this.element.addEventListener('wheel', (e) => this.onWheel(e));
  }

  protected onMouseDown(e: MouseEvent) {
    this.mouseDownX = this.getMouseX(e);
  }

  protected onMouseMove(e: MouseEvent) {
    if (this.mouseDownX !== -1) {
      const movedPx = this.mouseDownX - this.getMouseX(e);
      this.onPanned(movedPx);
      this.mouseDownX = this.getMouseX(e);
      e.preventDefault();
    }
    this.mouseXpos = this.getMouseX(e);
  }

  private getMouseX(e: MouseEvent) {
    return e.clientX - this.contentOffsetX;
  }

  private onMouseUp() {
    this.mouseDownX = -1;
  }

  private onWheel(e: WheelEvent) {
    if (e.deltaX) {
      this.onPanned(e.deltaX * ZoomContent.SCROLL_SPEED);
    }
  }
}