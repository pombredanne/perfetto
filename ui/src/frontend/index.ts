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

import '../tracks/all_frontend';

import * as m from 'mithril';

import {forwardRemoteCalls, Remote} from '../base/remote';
import {ObjectById, TrackState} from '../common/state';
import {State} from '../common/state';
import {warmupWasmEngineWorker} from '../controller/wasm_engine_proxy';

import {ControllerProxy} from './controller_proxy';
import {globals} from './globals';
import {HomePage} from './home_page';
import {QueryPage} from './query_page';
import {TrackDataStore} from './track_data_store';
import {ViewerPage} from './viewer_page';

function createController(): ControllerProxy {
  const worker = new Worker('controller_bundle.js');
  worker.onerror = e => {
    console.error(e);
  };
  const port = worker as {} as MessagePort;
  return new ControllerProxy(new Remote(port));
}

/**
 * The API the main thread exposes to the controller.
 */
class FrontendApi {
  updateState(state: State) {
    globals.state = state;
    m.redraw();
  }
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
      kind: trackType,
      height: 100,
      name: `Track ${i}`,
    };
  }
  return tracks;
}

/**
 * Generates random slices with duration between (0, maxDuratoin) and gap from
 * one slice to the next between (0, maxInterval).
 */
function generateRandomSlices(
    boundStart: number,
    boundEnd: number,
    maxDuration: number,
    maxInterval: number) {
  const slices = [];
  let nextSliceStart = boundStart;
  let i = 1;
  while (true) {
    const randDuration = Math.random() * maxDuration;
    const randInterval = Math.random() * maxInterval;

    const start = nextSliceStart;
    const end = start + randDuration;
    if (end > boundEnd) break;

    slices.push({start, end, title: `Slice ${i}`});

    i++;
    nextSliceStart = end + randInterval;
  }
  return slices;
}

function setDemoData(): void {
  const maxVisibleWidth = 1000000;
  const initialSliceWidth = maxVisibleWidth / 50;
  for (let i = 0; i < 10; i++) {
    if (i % 2 !== 0) continue;
    const d = {
      id: i.toString(),
      trackKind: 'CpuSliceTrack',
      data: {
        slices: generateRandomSlices(
            0, maxVisibleWidth, initialSliceWidth, initialSliceWidth),
      }
    };
    globals.trackDataStore.storeData(d);
  }
}

async function main() {
  globals.state = {i: 0, tracks: getDemoTracks()};
  globals.trackDataStore = new TrackDataStore();

  setDemoData();

  const controller = createController();
  const channel = new MessageChannel();
  await controller.initAndGetState(channel.port1);
  forwardRemoteCalls(channel.port2, new FrontendApi());

  globals.controller = controller;
  globals.dispatch = controller.dispatch.bind(controller);
  warmupWasmEngineWorker();

  const root = document.querySelector('main');
  if (!root) {
    console.error('root element not found.');
    return;
  }

  m.route(root, '/', {
    '/': HomePage,
    '/viewer': ViewerPage,
    '/query/:trace': QueryPage,
  });
}

main();
