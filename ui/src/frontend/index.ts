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

import {forwardRemoteCalls} from '../base/remote';
import {setState} from '../common/actions';
import {loadState} from '../common/permalinks';
import {State} from '../common/state';
import {
  takeWasmEngineWorkerPort,
  warmupWasmEngineWorker,
} from '../controller/wasm_engine_proxy';

import {globals, QuantizedLoad, ThreadDesc} from './globals';
import {HomePage} from './home_page';
import {ViewerPage} from './viewer_page';

/**
 * The API the main thread exposes to the controller.
 */
class FrontendApi {
  updateState(state: State) {
    globals.state = state;
    this.redraw();
  }

  publishOverviewData(data: {[key: string]: QuantizedLoad}) {
    for (const key of Object.keys(data)) {
      if (!globals.overviewStore.has(key)) {
        globals.overviewStore.set(key, []);
      }
      globals.overviewStore.get(key)!.push(data[key]);
    }
    globals.rafScheduler.scheduleOneRedraw();
  }

  publishTrackData(args: {id: string, data: {}}) {
    globals.trackDataStore.set(args.id, args.data);
    globals.rafScheduler.scheduleOneRedraw();
  }

  publishQueryResult(args: {id: string, data: {}}) {
    globals.queryResults.set(args.id, args.data);
    this.redraw();
  }

  publishThreads(data: ThreadDesc[]) {
    globals.threads.clear();
    data.forEach(thread => {
      globals.threads.set(thread.utid, thread);
    });
    this.redraw();
  }

  /**
   * Creates a new trace processor wasm engine (backed by a worker running
   * engine_bundle.js) and returns a MessagePort for talking to it.
   * This indirection is due to workers not being able create workers in
   * Chrome which is tracked at: crbug.com/31666
   * TODO(hjd): Remove this once the fix has landed.
   */
  createWasmEnginePort(): MessagePort {
    return takeWasmEngineWorkerPort();
  }

  private redraw(): void {
    if (globals.state.route && globals.state.route !== m.route.get()) {
      m.route.set(globals.state.route);
    } else {
      m.redraw();
    }
  }
}

function main() {
  const controller = new Worker('controller_bundle.js');
  controller.onerror = e => {
    console.error(e);
  };
  const channel = new MessageChannel();
  forwardRemoteCalls(channel.port2, new FrontendApi());
  controller.postMessage(channel.port1, [channel.port1]);

  globals.initialize(controller.postMessage.bind(controller));

  warmupWasmEngineWorker();

  m.route(document.body, '/', {
    '/': HomePage,
    '/viewer': ViewerPage,
  });

  // Put these variables in the global scope for better debugging.
  (window as {} as {m: {}}).m = m;
  (window as {} as {globals: {}}).globals = globals;

  // Prevent pinch zoom.
  document.body.addEventListener('wheel', (e: MouseEvent) => {
    if (e.ctrlKey) e.preventDefault();
  });

  const stateHash = m.route.param('s');
  if (stateHash) {
    loadState(stateHash).then(state => globals.dispatch(setState(state)));
  }
}

main();
