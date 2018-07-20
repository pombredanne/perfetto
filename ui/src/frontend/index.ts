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

import {ObjectById, TrackState} from '../common/state';
import {warmupWasmEngineWorker} from '../controller/wasm_engine_proxy';
import {createAllTracksRegistry} from '../tracks/all_tracks';
import {CpuCounterTrack} from '../tracks/cpu_counters/frontend';
import {CpuSliceTrack} from '../tracks/cpu_slices/frontend';

import {CanvasController} from './canvas_controller';
import {CanvasWrapper} from './canvas_wrapper';
import {ChildVirtualContext} from './child_virtual_context';
import {globals} from './globals';
import {HomePage} from './home_page';
import {createPage} from './pages';
import {QueryPage} from './query_page';
import {ScrollableContainer} from './scrollable_container';
import {Track} from './track';

export const Frontend = {
  oninit() {
    this.width = 0;
    this.height = 0;
    this.canvasController = new CanvasController();
  },
  oncreate(vnode) {
    this.onResize = () => {
      const rect = vnode.dom.getBoundingClientRect();
      this.width = rect.width;
      this.height = rect.height;
      this.canvasController.setDimensions(this.width, this.height);
      m.redraw();
    };
    // Have to redraw after initialization to provide dimensions to view().
    setTimeout(() => this.onResize());

    // Once ResizeObservers are out, we can stop accessing the window here.
    window.addEventListener('resize', this.onResize);
  },
  onremove() {
    window.removeEventListener('resize', this.onResize);
  },
  view({}) {
    const canvasTopOffset = this.canvasController.getCanvasTopOffset();
    const ctx = this.canvasController.getContext();
    this.canvasController.clear();
    const tracks = globals.state.tracks;

    const trackVNodes: m.Children[] = [];

    let trackYOffset = 0;
    for (const trackState of Object.values(tracks)) {
      trackVNodes.push(m(Track, {
        name: `Track ${trackState.id}`,
        trackContext: new ChildVirtualContext(ctx, {
          y: trackYOffset,
          x: 0,
          width: this.width,
          height: trackState.height
        }),
        top: trackYOffset,
        width: this.width,
        trackState,
      }));
      trackYOffset += trackState.height;
    }

    return m(
        '.frontend',
        {
          style: {
            position: 'relative',
            width: '100%',
            height: 'calc(100% - 100px)',
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
          ...trackVNodes));
  },
} as m.Component<{width: number, height: number}, {
  canvasController: CanvasController,
  width: number,
  height: number,
  onResize: () => void
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
    if (i % 2 === 0) {
      trackType = CpuSliceTrack.type;
    } else {
      trackType = CpuCounterTrack.type;
    }
    tracks[i] = {
      id: i.toString(),
      type: trackType,
      height: 100,
    };
  }
  return tracks;
}

function main() {
  globals.state = {i: 0, tracks: getDemoTracks()};
  const worker = createController();
  // tslint:disable-next-line deprecation
  globals.dispatch = action => worker.postMessage(action);
  globals.trackRegistry = createAllTracksRegistry();
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
