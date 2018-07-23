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

import {createEmptyState} from '../common/state';
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
    const zoomContent = new ZoomContent(
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

    zoomContent.init();
  },
  onremove() {
    window.removeEventListener('resize', this.onResize);
  },
  view({}) {
    const canvasTopOffset = this.canvasController.getCanvasTopOffset();
    const ctx = this.canvasController.getContext();

    this.canvasController.clear();

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
          m(Track, {
            name: 'Track 1',
            trackContext: new ChildVirtualContext(
                ctx, {y: 0, x: 0, width: this.width, height: 90}),
            top: 0,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 2',
            trackContext: new ChildVirtualContext(
                ctx, {y: 100, x: 0, width: this.width, height: 90}),
            top: 100,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 3',
            trackContext: new ChildVirtualContext(
                ctx, {y: 200, x: 0, width: this.width, height: 90}),
            top: 200,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 4',
            trackContext: new ChildVirtualContext(
                ctx, {y: 300, x: 0, width: this.width, height: 90}),
            top: 300,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 5',
            trackContext: new ChildVirtualContext(
                ctx, {y: 400, x: 0, width: this.width, height: 90}),
            top: 400,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 6',
            trackContext: new ChildVirtualContext(
                ctx, {y: 500, x: 0, width: this.width, height: 90}),
            top: 500,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 7',
            trackContext: new ChildVirtualContext(
                ctx, {y: 600, x: 0, width: this.width, height: 90}),
            top: 600,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 8',
            trackContext: new ChildVirtualContext(
                ctx, {y: 700, x: 0, width: this.width, height: 90}),
            top: 700,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 9',
            trackContext: new ChildVirtualContext(
                ctx, {y: 800, x: 0, width: this.width, height: 90}),
            top: 800,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }),
          m(Track, {
            name: 'Track 10',
            trackContext: new ChildVirtualContext(
                ctx, {y: 900, x: 0, width: this.width, height: 90}),
            top: 900,
            width: this.width,
            timeScale: this.timeScale,
            visibleWindowMs: this.visibleWindowMs
          }), ), );
  },
} as m.Component<{width: number, height: number}, {
  canvasController: CanvasController,
  width: number,
  height: number,
  onResize: () => void,
  timeScale: TimeScale,
  visibleWindowMs: {start: number, end: number}
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

function main() {
  globals.state = createEmptyState();
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
