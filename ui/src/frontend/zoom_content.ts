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

/**
 * Enables horizontal pan and zoom with mouse-based drag and WASD navigation.
 */
export class ZoomContent {
  static ZOOM_IN_PERCENTAGE_SPEED = 0.95;
  static ZOOM_OUT_PERCENTAGE_SPEED = 1.05;
  static KEYBOARD_PAN_PX_PER_FRAME = 20;
  static HORIZONTAL_WHEEL_PAN_SPEED = 1;

  // Usually, animations are cancelled on keyup. However, in case the keyup
  // event is not captured by the document, e.g. if it loses focus first, then
  // we want to stop the animation as soon as possible.
  static ANIMATION_AUTO_END_AFTER_INITIAL_KEYPRESS_MS = 700;
  static ANIMATION_AUTO_END_AFTER_KEYPRESS_MS = 80;

  // This defines the step size for an individual pan or zoom keyboard tap.
  static TAP_ANIMATION_TIME = 200;

  static PAN_LEFT_KEYS = ['a'];
  static PAN_RIGHT_KEYS = ['d'];
  static PAN_KEYS =
      ZoomContent.PAN_LEFT_KEYS.concat(ZoomContent.PAN_RIGHT_KEYS);
  static ZOOM_IN_KEYS = ['w'];
  static ZOOM_OUT_KEYS = ['s'];
  static ZOOM_KEYS = ZoomContent.ZOOM_IN_KEYS.concat(ZoomContent.ZOOM_OUT_KEYS);

  protected mouseDownPositionX = -1;
  private mousePositionX = -1;

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
    let directionFactor = 0;
    let tapCancelTimeout: Timer;

    const panAnimation = new Animation(() => {
      this.onPanned(directionFactor * ZoomContent.KEYBOARD_PAN_PX_PER_FRAME);
    });

    document.body.addEventListener('keydown', (e) => {
      console.log(e);
      if (ZoomContent.PAN_KEYS.indexOf(e.key) !== -1) {
        directionFactor =
            ZoomContent.PAN_LEFT_KEYS.indexOf(e.key) !== -1 ? -1 : 1;
        const animationTime = e.repeat ?
            ZoomContent.ANIMATION_AUTO_END_AFTER_KEYPRESS_MS :
            ZoomContent.ANIMATION_AUTO_END_AFTER_INITIAL_KEYPRESS_MS;
        panAnimation.start(animationTime);
        clearTimeout(tapCancelTimeout);
      }
    });
    document.body.addEventListener('keyup', (e) => {
      if (ZoomContent.PAN_KEYS.indexOf(e.key) !== -1) {
        const cancellingDirectionFactor =
            ZoomContent.PAN_LEFT_KEYS.indexOf(e.key) !== -1 ? -1 : 1;

        // Only cancel if the lifted key is the one controlling the animation.
        if (cancellingDirectionFactor === directionFactor) {
          const minEndTime =
              panAnimation.getStartTimeMs() + ZoomContent.TAP_ANIMATION_TIME;
          const waitTime = minEndTime - Date.now();
          tapCancelTimeout = setTimeout(() => panAnimation.stop(), waitTime);
        }
      }
    });
  }

  private handleKeyZooming() {
    let zoomingIn = true;
    let tapCancelTimeout: Timer;

    const zoomAnimation = new Animation(() => {
      const percentage = zoomingIn ? ZoomContent.ZOOM_IN_PERCENTAGE_SPEED :
                                     ZoomContent.ZOOM_OUT_PERCENTAGE_SPEED;
      this.onZoomed(this.mousePositionX, percentage);
    });

    document.body.addEventListener('keydown', (e) => {
      if (ZoomContent.ZOOM_KEYS.indexOf(e.key) !== -1) {
        zoomingIn = ZoomContent.ZOOM_IN_KEYS.indexOf(e.key) !== -1;
        const animationTime = e.repeat ?
            ZoomContent.ANIMATION_AUTO_END_AFTER_KEYPRESS_MS :
            ZoomContent.ANIMATION_AUTO_END_AFTER_INITIAL_KEYPRESS_MS;
        zoomAnimation.start(animationTime);
        clearTimeout(tapCancelTimeout);
      }
    });
    document.body.addEventListener('keyup', (e) => {
      if (ZoomContent.ZOOM_KEYS.indexOf(e.key) !== -1) {
        const cancellingZoomIn = ZoomContent.ZOOM_IN_KEYS.indexOf(e.key) !== -1;

        // Only cancel if the lifted key is the one controlling the animation.
        if (cancellingZoomIn === zoomingIn) {
          const minEndTime =
              zoomAnimation.getStartTimeMs() + ZoomContent.TAP_ANIMATION_TIME;
          const waitTime = minEndTime - Date.now();
          tapCancelTimeout = setTimeout(() => zoomAnimation.stop(), waitTime);
        }
      }
    });
  }

  private attachMouseEventListeners() {
    this.element.addEventListener('mousedown', (e) => this.onMouseDown(e));
    this.element.addEventListener('mousemove', (e) => this.onMouseMove(e));
    this.element.addEventListener('mouseup', () => this.onMouseUp());
    this.element.addEventListener('wheel', (e) => this.onWheel(e));
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
      this.onPanned(e.deltaX * ZoomContent.HORIZONTAL_WHEEL_PAN_SPEED);
    }
  }
}