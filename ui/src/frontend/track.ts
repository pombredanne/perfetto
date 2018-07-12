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

import {CpuSlicesFrontend} from '../tracks/cpu_slices/frontend';
import {OffsetTimeScale, TimeScale} from './time_scale';
import {TrackShell} from './track_shell';
import {VirtualCanvasContext} from './virtual_canvas_context';

export const Track = {
  oncreate(vdom) {
    const domContent = vdom.dom.querySelector('.dom-content');
    if (!domContent) {
      throw Error('Could not create Track DOM content container');
    }
    const bcr = domContent.getBoundingClientRect();
    this.x.setOffset(bcr.left * -1);
    this.x.setWidth(bcr.width);
  },
  view({attrs}) {
    if (!this.x) {
      this.x = new OffsetTimeScale(attrs.timeScale, 0, 1000);
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
          m('.dom-content', {style: {width: '100%'}}, m(CpuSlicesFrontend, {
              name: attrs.name,
              trackContext: attrs.trackContext,
              x: this.x
            }))));
  }
} as
    m.Component < {name: string, trackContext: VirtualCanvasContext, top: number
  timeScale: TimeScale
}, {x: OffsetTimeScale}>;
