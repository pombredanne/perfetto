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

import {moveTrack, toggleTrackPinned} from '../common/actions';
import {Action} from '../common/actions';
import {TrackState} from '../common/state';

import {globals} from './globals';
import {drawGridLines} from './gridline_helper';
import {quietDispatch} from './mithril_helpers';
import {Panel} from './panel';
import {Track} from './track';
import {trackRegistry} from './track_registry';

// TODO(hjd): We should remove the constant where possible.
// If any uses can't be removed we should read this constant from CSS.
export const TRACK_SHELL_WIDTH = 300;

function isPinned(id: string) {
  return globals.state.pinnedTracks.indexOf(id) !== -1;
}

const TrackShell = {
  view({attrs}) {
    return m(
        '.track-shell',
        m('h1', attrs.trackState.name),
        m(TrackButton, {
          action: moveTrack(attrs.trackState.id, 'up'),
          i: 'arrow_upward_alt',
        }),
        m(TrackButton, {
          action: moveTrack(attrs.trackState.id, 'down'),
          i: 'arrow_downward_alt',
        }),
        m(TrackButton, {
          action: toggleTrackPinned(attrs.trackState.id),
          i: isPinned(attrs.trackState.id) ? 'star' : 'star_border',
        }));
  },
} as m.Component<{trackState: TrackState}>;

const TrackContent = {
  view({attrs}) {
    return m('.track-content', {
      onmousemove: (e: MouseEvent) => {
        attrs.track.onMouseMove({x: e.layerX, y: e.layerY});
        globals.rafScheduler.scheduleRedraw();
      },
      onmouseout: () => {
        attrs.track.onMouseOut();
        globals.rafScheduler.scheduleRedraw();
      },
    }, );
  }
} as m.Component<{track: Track}>;

const TrackComponent = {
  view({attrs}) {
    return m('.track', [
      m(TrackShell, {trackState: attrs.trackState}),
      m(TrackContent, {track: attrs.track})
    ]);
  }
} as m.Component<{trackState: TrackState, track: Track}>;

const TrackButton = {
  view({attrs}) {
    return m(
        'i.material-icons.track-button',
        {
          onclick: quietDispatch(attrs.action),
        },
        attrs.i);
  }
} as m.Component<{
  action: Action,
  i: string,
},
                    {}>;

export class TrackPanel extends Panel {
  private track: Track;
  constructor(public trackState: TrackState) {
    // TODO: Since ES6 modules are asynchronous and it is conceivable that we
    // want to load a track implementation on demand, we should not rely here on
    // the fact that the track is already registered. We should show some
    // default content until a track implementation is found.
    super();
    const trackCreator = trackRegistry.get(this.trackState.kind);
    this.track = trackCreator.create(this.trackState);
  }

  getHeight(): number {
    return this.track.getHeight();
  }

  updateDom(dom: HTMLElement): void {
    // TODO: Let tracks render DOM in the content area.
    m.render(
        dom,
        m(TrackComponent, {trackState: this.trackState, track: this.track}));
  }

  renderCanvas(ctx: CanvasRenderingContext2D) {
    ctx.translate(TRACK_SHELL_WIDTH, 0);
    drawGridLines(
        ctx,
        globals.frontendLocalState.timeScale,
        globals.frontendLocalState.visibleWindowTime,
        this.track.getHeight());

    this.track.renderCanvas(ctx);
  }
}
