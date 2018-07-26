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

import {forwardRemoteCalls, Remote} from '../base/remote';
import {State} from '../common/state';
import {warmupWasmEngineWorker, createWasmEngineWorkerPort} from '../controller/wasm_engine_proxy';
import {ControllerProxy} from './controller_proxy';
import {globals} from './globals';
import {HomePage} from './home_page';
import {QueryPage} from './query_page';
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
    console.log(state);
    if (state.route && state.route !== m.route.get()) {
      m.route.set(state.route);
    } else {
      m.redraw();
    }
  }

  /**
   * Creates a new trace processor wasm engine (backed by a worker running
   * engine_bundle.js) and returns a MessagePort for talking to it.
   * This indirection is due to workers not being able create workers in
   * Chrome which is tracked at: crbug.com/31666
   * TODO(hjd): Remove this once the fix has landed.
   */
  createWasmEnginePort(): MessagePort {
    return createWasmEngineWorkerPort();
  }
}

async function main() {
  const controller = createController();
  const channel = new MessageChannel();
  forwardRemoteCalls(channel.port2, new FrontendApi());
  globals.controller = controller;
  globals.state = await controller.initAndGetState(channel.port1);
  globals.dispatch = controller.dispatch.bind(controller);
  warmupWasmEngineWorker();

  const root = document.getElementById('frontend');
  if (!root) {
    console.error('root element not found.');
    return;
  }

  m.route(root, '/', {
    '/': HomePage,
    '/viewer': ViewerPage,
    '/query/:trace': QueryPage,
  });

  (window as any).m = m;
  (window as any).globals = globals;
}

main();
