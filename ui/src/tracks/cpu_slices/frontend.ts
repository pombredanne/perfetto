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

import {
  Nanoseconds,
  OffsetTimeScale,
} from '../../frontend/time_scale';
import {TrackCanvasContent} from '../../frontend/track_canvas_content';
import {VirtualCanvasContext} from '../../frontend/virtual_canvas_context';

import {CpuSlicesTrackCanvasContent} from './canvas_content';

export const CpuSlicesFrontend = {
  view({attrs}) {
    if (!this.canvasContent) {
      this.canvasContent = new CpuSlicesTrackCanvasContent(attrs.x);
    }
    if (attrs.trackContext.isOnCanvas()) {
      this.canvasContent.render(attrs.trackContext, {trackName: attrs.name});
    }

    const sliceStart: Nanoseconds = 100000;
    const sliceEnd: Nanoseconds = 400000;
    const rectStart = attrs.x.tsToPx(sliceStart);
    const rectWidth = attrs.x.tsToPx(sliceEnd) - rectStart;

    return m(
        '.dom-content',
        {style: {width: '100%'}},
        m('.marker',
          {
            style: {
              'font-size': '1.5em',
              position: 'absolute',
              left: rectStart.toString() + 'px',
              width: rectWidth.toString() + 'px',
              background: '#aca'
            }
          },
          attrs.name + ' DOM Content'));
  }
} as
    m.Component<
        {name: string, trackContext: VirtualCanvasContext, x: OffsetTimeScale},
        {canvasContent: TrackCanvasContent}>;
