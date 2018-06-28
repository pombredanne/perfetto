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

import * as m from 'mithril';
import Track from './track';
import CanvasWrapper from './canvas_wrapper';

const Frontend = {
  view({attrs}) {
    return m('.frontend',
      {
        style: {
          border: "1px solid #ccc",
          padding: "20px",
          position: 'relative',
          width: attrs.width + 'px'
        }
      },
      m(Track, { name: 'Track 123' }),
      m(CanvasWrapper, {
        width: attrs.width,
        height: attrs.height
      })
    );
  }
} as m.Comp<{width: number, height: number}>;

export default Frontend;