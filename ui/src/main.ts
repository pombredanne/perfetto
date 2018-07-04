/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import * as m from 'mithril';
import { frontend } from './frontend';
import { Engine } from './engine';
import { WasmEngineProxy, warmupWasmEngineWorker }
    from './engine/wasm_engine_proxy';

import { Action, updateQueryAction} from './actions';
import { State, createZeroState } from './state';

console.log('Hello from the main thread!');

function createController(): Worker {
  const worker = new Worker("worker_bundle.js");
  worker.onerror = e => {
    console.error(e);
  };
  return worker;
}

function createFrontend() {
  const root = document.getElementById('frontend');
  if (!root) {
    console.error('root element not found.');
    return;
  }
  const rect = root.getBoundingClientRect();

  m.render(root, m(frontend, {
    width: rect.width,
    height: rect.height,
  }));
}

class Dispatcher {
  constructor(private worker: Worker) {
  }

  dispatch(action: Action) {
    console.log(action);
    this.worker.postMessage(action);
  }
}

class FrontendStateStore {
  private state: State;
  constructor() {
    this.state = createZeroState();
  }

  updateState(state: State) {
    this.state = state;
    console.log('re-imported', this.state);
  }
}

function main(input: Element, button: Element) {
  const worker = createController();
  const dispatcher = new Dispatcher(worker);
  const frontendStateStore = new FrontendStateStore();
  createFrontend();

  warmupWasmEngineWorker();
  button.addEventListener('click', () => {
    // engine.rawQuery({
    //   sqlQuery: 'select * from sched;',
    // }).then(
    //   result => console.log(result)
    // );
    console.log("I'm being clicked!")
    dispatcher.dispatch(updateQueryAction("select * from sched;"));
  });

  worker.onmessage = (message: MessageEvent) => {
    console.log(message, 'in main thread');
    const state = message.data as State;
    console.log(state);
    frontendStateStore.updateState(state);
  }

  // tslint:disable-next-line:no-any
  input.addEventListener('change', (e: any) => {
    const blob: Blob = e.target.files.item(0);
    if (blob === null) return;
    const engine: Engine = WasmEngineProxy.create(blob);
    engine;

  });
}
const input = document.querySelector('#trace');
const button = document.querySelector('#query');
if (input && button) {
  main(input, button);
}
