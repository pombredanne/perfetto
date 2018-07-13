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

import * as m from 'mithril';

import {Nanoseconds, OffsetTimeScale, TimeScale} from './time_scale';
import {TrackShell} from './track_shell';
import {VirtualCanvasContext} from './virtual_canvas_context';

export const Track = {
  view({attrs}) {
    const sliceStart: Nanoseconds = 100000;
    const sliceEnd: Nanoseconds = 400000;

    const rectStart = attrs.x.tsToPx(sliceStart);
    const rectWidth = attrs.x.tsToPx(sliceEnd) - rectStart;

    // TODO: This offset should be based on TrackShell. Need Track Refactoring.
    const domX = new OffsetTimeScale(attrs.x, 200, 800);
    const domStart = domX.tsToPx(sliceStart);
    const domWidth = domX.tsToPx(sliceEnd) - domStart;

    if (attrs.trackContext.isOnCanvas()) {
      attrs.trackContext.fillStyle = '#ccc';
      attrs.trackContext.fillRect(0, 0, 1000, 73);

      attrs.trackContext.fillStyle = '#c00';
      attrs.trackContext.fillRect(rectStart, 40, rectWidth, 30);

      attrs.trackContext.font = '16px Arial';
      attrs.trackContext.fillStyle = '#000';
      attrs.trackContext.fillText(
          attrs.name + ' rendered by canvas', rectStart, 60);
    }

    return m(
        '.track',
        {
          style: {
            position: 'absolute',
            top: attrs.top.toString() + 'px',
            left: 0,
            width: '100%'
          }
        },
        m(TrackShell,
          attrs,
          m('.marker',
            {
              style: {
                'font-size': '1.5em',
                position: 'absolute',
                left: domStart.toString() + 'px',
                width: domWidth.toString() + 'px',
                background: '#aca'
              }
            },
            attrs.name + ' DOM Content')));
  }
} as m.Component<{
  name: string,
  trackContext: VirtualCanvasContext,
  top: number,
  x: TimeScale
}>;
