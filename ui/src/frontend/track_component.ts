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

import {CanvasController} from './canvas_controller';
import {globals} from './globals';
import {Milliseconds, TimeScale} from './time_scale';
import {Track} from './track';
import {trackRegistry} from './track_registry';

interface TrackComponentAttrs {
  canvasController: CanvasController;
  top: number;
  width: number;
  timeScale: TimeScale;
  trackState: TrackState;
  visibleWindowMs: {start: number, end: number};
}

/**
 * Passes the necessary handles and data to Track so it can render canvas and
 * DOM.
 */
function renderTrack(attrs: TrackComponentAttrs, track: Track) {
  // TODO(dproy): Figure out how track implementations should render DOM.
  const trackData = globals.trackDataStore.get(attrs.trackState.id);
  if (trackData !== undefined) track.consumeData(trackData);

  const trackBounds = {
    yStart: attrs.top,
    yEnd: attrs.top + attrs.trackState.height,
  };

  if (attrs.canvasController.isYBoundsOnCanvas(trackBounds)) {
    // We either have
    //
    // -------------------------------- canvas
    //   |
    //   |  canvasYStart (negative here, because
    //   |                we overdraw the canvas on top as well as bottom)
    //   |
    // -------------------------------- ScrollableTrackDisplay top
    //   |
    //   |  trackYStart (track.attrs.top)
    //   |
    // -------------------------------- track
    //
    // OR,
    //
    // -------------------------------- ScrollableTrackDisplay top
    //   |      |
    //   |      |  canvasYStart (positive here)
    //   |      |
    //   |     ------------------------- ScrollableTrackDisplay top
    //   |
    //   |  trackYStart (track.attrs.top)
    //   |
    // -------------------------------- track
    //
    // In both cases, trackYStartOnCanvas for track is
    // trackYStart - canvasYStart.
    const canvasYStart = attrs.canvasController.getCanvasTopOffset();
    const trackYStart = attrs.top;
    const trackYStartOnCanvas = trackYStart - canvasYStart;

    // Translate and clip the canvas context.
    const ctx = attrs.canvasController.get2DContext();
    ctx.save();
    ctx.translate(0, trackYStartOnCanvas);
    const clipRect = new Path2D();
    clipRect.rect(0, 0, attrs.width, attrs.trackState.height);
    ctx.clip(clipRect);

    track.renderCanvas(
        ctx, attrs.width, attrs.timeScale, attrs.visibleWindowMs);

    ctx.restore();
  }
}

export const TrackComponent = {
  oninit({attrs}) {
    // TODO: Since ES6 modules are asynchronous and it is conceivable that we
    // want to load a track implementation on demand, we should not rely here on
    // the fact that the track is already registered. We should show some
    // default content until a track implementation is found.
    const trackCreator = trackRegistry.get(attrs.trackState.kind);
    this.track = trackCreator.create(attrs.trackState);
  },

  view({attrs}) {

    const sliceStart: Milliseconds = 100000;
    const sliceEnd: Milliseconds = 400000;

    const rectStart = attrs.timeScale.msToPx(sliceStart);
    const rectWidth = attrs.timeScale.msToPx(sliceEnd) - rectStart;

    return m(
        '.track',
        {
          style: {
            border: '1px solid #666',
            position: 'absolute',
            top: attrs.top.toString() + 'px',
            left: 0,
            width: '100%',
            height: `${attrs.trackState.height}px`,
          }
        },
        m('.track-shell',
          {
            style: {
              background: '#fff',
              padding: '20px',
              width: '200px',
              'border-right': '1px solid #666',
              height: '100%',
              'z-index': '100',
              position: 'relative',
            }
          },
          m('h1',
            {style: {margin: 0, 'font-size': '1.5em'}},
            attrs.trackState.name)),
        m('.track-content',
          {
            style: {
              width: 'calc(100% - 200px)',
              height: '100%',
              position: 'absolute',
              left: '200px',
              top: '0'
            }
          },
          // TODO(dproy): Move out DOM Content from the track class.
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
            attrs.trackState.kind + ' DOM Content')));
  },


  oncreate({attrs}): void {
    renderTrack(attrs, this.track);
  },

  onupdate({attrs}) {
    renderTrack(attrs, this.track);
  }
} as m.Component<TrackComponentAttrs, {track: Track}>;
