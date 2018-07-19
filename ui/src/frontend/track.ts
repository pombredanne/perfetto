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
import {gTrackRegistry} from './globals';
import {TrackContent} from './track_content';
import {TrackShell} from './track_shell';
import {VirtualCanvasContext} from './virtual_canvas_context';

export abstract class TrackImpl {
  constructor(protected trackState: TrackState, public width: number) {}
  abstract draw(vCtx: VirtualCanvasContext): void;
}

export const Track = {
  oninit({attrs}) {
    const trackCreator = gTrackRegistry.get(attrs.trackState.type);
    if (trackCreator == null) {
      throw new Error(
          'No Track implementation found for type ' + attrs.trackState.type);
    }
    this.trackInstance = trackCreator(attrs.trackState, attrs.width);
  },

  view({attrs}) {
    // if (attrs.trackContext.isOnCanvas()) {
    //   attrs.trackContext.fillStyle = '#ccc';
    //   attrs.trackContext.fillRect(0, 0, attrs.width, 73);

    //   attrs.trackContext.font = '16px Arial';
    //   attrs.trackContext.fillStyle = '#000';
    //   attrs.trackContext.fillText(
    //       attrs.name + ' rendered by canvas', Math.round(attrs.width / 2),
    //       20);
    // }

    return m(
        '.track',
        {
          style: {
            position: 'absolute',
            top: attrs.top.toString() + 'px',
            left: 0,
            width: '100%',
            height: `${attrs.height} px`,
          }
        },
        m(TrackShell, attrs),
        m(TrackContent, {
          trackVirtualContext: attrs.trackContext,
          trackInstance: this.trackInstance,
          width: attrs.width
        }));
  }
} as m.Component<{
  name: string,
  trackContext: VirtualCanvasContext,
  top: number,
  // TODO: Delete dups from trackState
  width: number,
  height: number,
  trackState: TrackState,
},
                     {trackInstance: TrackImpl}>;
