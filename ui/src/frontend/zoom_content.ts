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

import {Animation} from './animation';
import Timer = NodeJS.Timer;

const ZOOM_IN_PERCENTAGE_SPEED = 0.95;
const ZOOM_OUT_PERCENTAGE_SPEED = 1.05;
const KEYBOARD_PAN_PX_PER_FRAME = 20;
const HORIZONTAL_WHEEL_PAN_SPEED = 1;

// Usually, animations are cancelled on keyup. However, in case the keyup
// event is not captured by the document, e.g. if it loses focus first, then
// we want to stop the animation as soon as possible.
const ANIMATION_AUTO_END_AFTER_INITIAL_KEYPRESS_MS = 700;
const ANIMATION_AUTO_END_AFTER_KEYPRESS_MS = 80;

// This defines the step size for an individual pan or zoom keyboard tap.
const TAP_ANIMATION_TIME = 200;

const PAN_LEFT_KEYS = ['a'];
const PAN_RIGHT_KEYS = ['d'];
const PAN_KEYS = PAN_LEFT_KEYS.concat(PAN_RIGHT_KEYS);
const ZOOM_IN_KEYS = ['w'];
const ZOOM_OUT_KEYS = ['s'];
const ZOOM_KEYS = ZOOM_IN_KEYS.concat(ZOOM_OUT_KEYS);

/**
 * Enables horizontal pan and zoom with mouse-based drag and WASD navigation.
 */
export class ZoomContent {
  private mouseDownPositionX = -1;
  private mousePositionX = -1;

  private onMouseDownLambda = (e: MouseEvent) => this.onMouseDown(e);
  private onMouseMoveLambda = (e: MouseEvent) => this.onMouseMove(e);
  private onMouseUpLambda = () => this.onMouseUp();
  private onWheelLambda = (e: WheelEvent) => this.onWheel(e);

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

  shutdown() {
    this.detachMouseEventListeners();
  }

  private handleKeyPanning() {
    let directionFactor = 0;
    let tapCancelTimeout: Timer;

    const panAnimation = new Animation(() => {
      this.onPanned(directionFactor * KEYBOARD_PAN_PX_PER_FRAME);
    });

    document.body.addEventListener('keydown', e => {
      if (!PAN_KEYS.includes(e.key)) {
        return;
      }
      directionFactor = PAN_LEFT_KEYS.includes(e.key) ? -1 : 1;
      const animationTime = e.repeat ?
          ANIMATION_AUTO_END_AFTER_KEYPRESS_MS :
          ANIMATION_AUTO_END_AFTER_INITIAL_KEYPRESS_MS;
      panAnimation.start(animationTime);
      clearTimeout(tapCancelTimeout);
    });
    document.body.addEventListener('keyup', e => {
      if (!PAN_KEYS.includes(e.key)) {
        return;
      }
      const cancellingDirectionFactor = PAN_LEFT_KEYS.includes(e.key) ? -1 : 1;

      // Only cancel if the lifted key is the one controlling the animation.
      if (cancellingDirectionFactor === directionFactor) {
        const minEndTime = panAnimation.getStartTimeMs() + TAP_ANIMATION_TIME;
        const waitTime = minEndTime - Date.now();
        tapCancelTimeout = setTimeout(() => panAnimation.stop(), waitTime);
      }
    });
  }

  private handleKeyZooming() {
    let zoomingIn = true;
    let tapCancelTimeout: Timer;

    const zoomAnimation = new Animation(() => {
      const percentage =
          zoomingIn ? ZOOM_IN_PERCENTAGE_SPEED : ZOOM_OUT_PERCENTAGE_SPEED;
      this.onZoomed(this.mousePositionX, percentage);
    });

    document.body.addEventListener('keydown', e => {
      if (ZOOM_KEYS.includes(e.key)) {
        zoomingIn = ZOOM_IN_KEYS.includes(e.key);
        const animationTime = e.repeat ?
            ANIMATION_AUTO_END_AFTER_KEYPRESS_MS :
            ANIMATION_AUTO_END_AFTER_INITIAL_KEYPRESS_MS;
        zoomAnimation.start(animationTime);
        clearTimeout(tapCancelTimeout);
      }
    });
    document.body.addEventListener('keyup', e => {
      if (ZOOM_KEYS.includes(e.key)) {
        const cancellingZoomIn = ZOOM_IN_KEYS.includes(e.key);

        // Only cancel if the lifted key is the one controlling the animation.
        if (cancellingZoomIn === zoomingIn) {
          const minEndTime =
              zoomAnimation.getStartTimeMs() + TAP_ANIMATION_TIME;
          const waitTime = minEndTime - Date.now();
          tapCancelTimeout = setTimeout(() => zoomAnimation.stop(), waitTime);
        }
      }
    });
  }

  private attachMouseEventListeners() {
    this.element.addEventListener('mousedown', this.onMouseDownLambda);
    this.element.addEventListener('mousemove', this.onMouseMoveLambda);
    this.element.addEventListener('mouseup', this.onMouseUpLambda);
    this.element.addEventListener('wheel', this.onWheelLambda);
  }

  private detachMouseEventListeners() {
    this.element.removeEventListener('mousedown', this.onMouseDownLambda);
    this.element.removeEventListener('mousemove', this.onMouseMoveLambda);
    this.element.removeEventListener('mouseup', this.onMouseUpLambda);
    this.element.removeEventListener('wheel', this.onWheelLambda);
  }

  protected onMouseDown(e: MouseEvent) {
    this.mouseDownPositionX = this.getMouseX(e);
  }

  protected onMouseMove(e: MouseEvent) {
    if (this.mouseDownPositionX !== -1) {
      this.onPanned(this.mouseDownPositionX - this.getMouseX(e));
      this.mouseDownPositionX = this.getMouseX(e);
      e.preventDefault();
    }
    this.mousePositionX = this.getMouseX(e);
  }

  private getMouseX(e: MouseEvent) {
    return e.clientX - this.contentOffsetX;
  }

  private onMouseUp() {
    this.mouseDownPositionX = -1;
  }

  private onWheel(e: WheelEvent) {
    if (e.deltaX) {
      this.onPanned(e.deltaX * HORIZONTAL_WHEEL_PAN_SPEED);
    }
  }
}