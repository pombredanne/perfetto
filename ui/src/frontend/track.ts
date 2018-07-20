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

import {TrackState} from '../common/state';

import {globals} from './globals';
import {TrackContent} from './track_content';
import {TrackImpl} from './track_impl';
import {TrackShell} from './track_shell';
import {VirtualCanvasContext} from './virtual_canvas_context';

export const Track = {
  oninit({attrs}) {
    const trackCreator =
        globals.trackRegistry.getCreator(attrs.trackState.type);
    if (trackCreator == null) {
      throw new Error(
          'No Track implementation found for type ' + attrs.trackState.type);
    }
    this.trackImpl = trackCreator.create(attrs.trackState, attrs.width);
  },

  view({attrs}) {
    return m(
        '.track',
        {
          style: {
            position: 'absolute',
            top: attrs.top.toString() + 'px',
            left: 0,
            width: '100%',
            height: `${attrs.trackState.height} px`,
          }
        },
        m(TrackShell, attrs),
        m(TrackContent, {
          trackVirtualContext: attrs.trackContext,
          trackImpl: this.trackImpl,
          width: attrs.width
        }));
  }
} as m.Component<{
  name: string,
  trackContext: VirtualCanvasContext,
  top: number,
  width: number,
  trackState: TrackState,
},
                     // TODO(dproy): Fix formatter. This is ridiculous.
                     {trackImpl: TrackImpl}>;
