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

export class CanvasOffsetRect {

  public height: number;
  public top: number = 0;
  public readonly left = 0;

  constructor(private winHeight: number, public width: number, public heightFactor = 2) {

    this.height = this.winHeight * heightFactor;
  }

  setScrollTop(scrollTop: number)
  {
    const extraHeight = this.height - this.winHeight;
    this.top = scrollTop - Math.round(extraHeight / 2);
  }
}
