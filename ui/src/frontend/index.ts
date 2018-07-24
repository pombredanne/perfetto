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

import '../tracks/all_tracks';

import * as m from 'mithril';

import {ObjectById, TrackState} from '../common/state';
import {warmupWasmEngineWorker} from '../controller/wasm_engine_proxy';

import {CanvasController} from './canvas_controller';
import {CanvasWrapper} from './canvas_wrapper';
import {ChildVirtualContext} from './child_virtual_context';
import {globals} from './globals';
import {HomePage} from './home_page';
import {createPage} from './pages';
import {QueryPage} from './query_page';
import {ScrollableContainer} from './scrollable_container';
import {TimeScale} from './time_scale';
import {Track} from './track';
import {ZoomContent} from './zoom_content';

export const Frontend = {
  oninit() {
    this.width = 0;
    this.height = 0;
    this.canvasController = new CanvasController();
    this.visibleWindowMs = {start: 0, end: 1000000};
    this.timeScale = new TimeScale(
        [this.visibleWindowMs.start, this.visibleWindowMs.end],
        [0, this.width]);
  },
  oncreate(vnode) {
    this.onResize = () => {
      const rect = vnode.dom.getBoundingClientRect();
      this.width = rect.width;
      this.height = rect.height;
      this.canvasController.setDimensions(this.width, this.height);
      this.timeScale.setLimitsPx(0, this.width);
      m.redraw();
    };
    // Have to redraw after initialization to provide dimensions to view().
    setTimeout(() => this.onResize());

    // Once ResizeObservers are out, we can stop accessing the window here.
    window.addEventListener('resize', this.onResize);


    // TODO: ContentOffsetX should be defined somewhere central.
    // Currently it lives here, in canvas wrapper, and in track shell.
    this.zoomContent = new ZoomContent(
        vnode.dom as HTMLElement,
        200,
        (pannedPx: number) => {
          const deltaMs = this.timeScale.deltaPxToDurationMs(pannedPx);
          this.visibleWindowMs.start += deltaMs;
          this.visibleWindowMs.end += deltaMs;
          this.timeScale.setLimitsMs(
              this.visibleWindowMs.start, this.visibleWindowMs.end);
          m.redraw();
        },
        (zoomedPositionPx: number, zoomPercentage: number) => {
          const totalTimespanMs =
              this.visibleWindowMs.end - this.visibleWindowMs.start;
          const newTotalTimespanMs = totalTimespanMs * zoomPercentage;

          const zoomedPositionMs =
              this.timeScale.pxToMs(zoomedPositionPx) as number;
          const positionPercentage =
              (zoomedPositionMs - this.visibleWindowMs.start) / totalTimespanMs;

          this.visibleWindowMs.start =
              zoomedPositionMs - newTotalTimespanMs * positionPercentage;
          this.visibleWindowMs.end =
              zoomedPositionMs + newTotalTimespanMs * (1 - positionPercentage);
          this.timeScale.setLimitsMs(
              this.visibleWindowMs.start, this.visibleWindowMs.end);
          m.redraw();
        });

    this.zoomContent.init();
  },
  onremove() {
    window.removeEventListener('resize', this.onResize);
    this.zoomContent.shutdown();
  },
  view() {
    const canvasTopOffset = this.canvasController.getCanvasTopOffset();
    const ctx = this.canvasController.getContext();

    this.canvasController.clear();
    const tracks = globals.state.tracks;

    const childTracks: m.Children[] = [];

    let trackYOffset = 0;
    for (const trackState of Object.values(tracks)) {
      childTracks.push(m(Track, {
        trackContext: new ChildVirtualContext(ctx, {
          y: trackYOffset,
          x: 0,
          width: this.width,
          height: trackState.height,
        }),
        top: trackYOffset,
        width: this.width,
        trackState,
        timeScale: this.timeScale,
        visibleWindowMs: this.visibleWindowMs
      }));
      trackYOffset += trackState.height;
    }

    return m(
        '.frontend',
        {
          style: {
            position: 'relative',
            width: '100%',
            height: 'calc(100% - 105px)',
            overflow: 'hidden'
          }
        },
        m(ScrollableContainer,
          {
            width: this.width,
            height: this.height,
            contentHeight: 1000,
            onPassiveScroll: (scrollTop: number) => {
              this.canvasController.updateScrollOffset(scrollTop);
              m.redraw();
            },
          },
          m(CanvasWrapper, {
            topOffset: canvasTopOffset,
            canvasElement: this.canvasController.getCanvasElement()
          }),
          ...childTracks));
  },
} as m.Component<{width: number, height: number}, {
  canvasController: CanvasController,
  width: number,
  height: number,
  onResize: () => void,
  timeScale: TimeScale,
  visibleWindowMs: {start: number, end: number},
  zoomContent: ZoomContent,
}>;

export const FrontendPage = createPage({
  view() {
    return m(Frontend, {width: 1000, height: 300});
  }
});

function createController(): Worker {
  const worker = new Worker('controller_bundle.js');
  worker.onerror = e => {
    console.error(e);
  };
  worker.onmessage = msg => {
    globals.state = msg.data;
    m.redraw();
  };
  return worker;
}

function getDemoTracks(): ObjectById<TrackState> {
  const tracks: {[key: string]: TrackState;} = {};
  for (let i = 0; i < 10; i++) {
    let trackType;
    // The track type strings here are temporary. They will be supplied by the
    // controller side track implementation.
    if (i % 2 === 0) {
      trackType = 'CpuSliceTrack';
    } else {
      trackType = 'CpuCounterTrack';
    }
    tracks[i] = {
      id: i.toString(),
      type: trackType,
      height: 100,
      name: `Track ${i}`,
    };
  }
  return tracks;
}

function main() {
  globals.state = {i: 0, tracks: getDemoTracks()};
  const worker = createController();
  // tslint:disable-next-line deprecation
  globals.dispatch = action => worker.postMessage(action);
  warmupWasmEngineWorker();

  const root = document.getElementById('frontend');
  if (!root) {
    console.error('root element not found.');
    return;
  }

  m.route(root, '/', {
    '/': HomePage,
    '/viewer': FrontendPage,
    '/query/:trace': QueryPage,
  });
}

main();
