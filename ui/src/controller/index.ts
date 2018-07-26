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

import {assertExists} from '../base/logging';
import {forwardRemoteCalls, Remote} from '../base/remote';
import {Action, addTrack} from '../common/actions';
import {createEmptyState, State, EngineConfig, TrackConfig} from '../common/state';
import {rootReducer} from './reducer';
import {WasmEngineProxy} from './wasm_engine_proxy';
import {Engine} from './engine';

type EngineControllerState = 'init'|'waiting_for_file'|'loading'|'ready'; 
class EngineController {
  private readonly config: EngineConfig;
  private readonly controller: Controller;
  private _state: EngineControllerState;
  private blob?: Blob;
  private engine?: Engine;

  constructor(config: EngineConfig, controller: Controller) {
    this.controller = controller;
    this.config = config;
    this._state = 'init';
    this.move('waiting_for_file');
  }

  get state(): EngineControllerState {
    return this._state;
  }

  move(newState: EngineControllerState) {
    switch (newState) {
      case 'waiting_for_file':
        this.controller.fetchBlob(this.config.url).then(blob => {
          this.blob = blob;
          this.move('loading');
        });
        break;
      case 'loading':
        const blob = assertExists<Blob>(this.blob);
        this.controller.createEngine(blob).then(engine => {
          this.engine = engine;
          this.move('ready');
        });
        break;
      case 'ready':
        const engine = assertExists<Engine>(this.engine);
        this.controller.dispatchMultiple([
          addTrack(this.config.id, 'CpuSliceTrack'),
          addTrack(this.config.id, 'CpuSliceTrack'),
          addTrack(this.config.id, 'CpuSliceTrack'),
          addTrack(this.config.id, 'CpuSliceTrack'),
          addTrack(this.config.id, 'CpuSliceTrack'),
          addTrack(this.config.id, 'CpuSliceTrack'),
          addTrack(this.config.id, 'CpuSliceTrack'),
        ]);
        engine.rawQuery({sqlQuery: 'select * from sched;'}).then((result) => {
          console.log(result);
        });
        break;
    }
    this._state = newState;
  }
}

class TrackController {
  private readonly config: TrackConfig;
  private readonly controller: Controller;
  private readonly engineController: EngineController;

  constructor(config: TrackConfig, controller: Controller, engineController: EngineController) {
    this.config = config;
    this.controller = controller;
    this.engineController = engineController;
  }

  foo() {
    console.log(this.config);
    console.log(this.controller);
    console.log(this.engineController)
  }
}

class Controller {
  private state: State;
  private _frontend?: FrontendProxy;
  private readonly localFiles: Map<string, File>;
  private readonly engines: Map<string, EngineController>;
  private readonly tracks: Map<string, TrackController>;

  constructor() {
    this.state = createEmptyState();
    this.localFiles = new Map();
    this.engines = new Map();
    this.tracks = new Map();
  }

  get frontend(): FrontendProxy {
    if (!this._frontend) throw new Error('No FrontendProxy');
    return this._frontend;
  }

  initAndGetState(frontendProxyPort: MessagePort): State {
    this._frontend = new FrontendProxy(new Remote(frontendProxyPort));
    return this.state;
  }

  dispatch(action: Action): void {
    this.dispatchMultiple([action]);
  }

  dispatchMultiple(actions: Action[]): void {
    //const oldState = this.state;
    for (const action of actions) {
      console.log(action);
      this.state = rootReducer(this.state, action);
    }

    for (const config of Object.values<EngineConfig>(this.state.engines)) {
      if (this.engines.has(config.id)) continue;
      this.engines.set(config.id, new EngineController(config, this));
    }

    for (const config of Object.values<TrackConfig>(this.state.tracks)) {
      if (this.tracks.has(config.id)) continue;
      const engine = assertExists<EngineController>(this.engines.get(config.engineId));
      this.tracks.set(config.id, new TrackController(config, this, engine));
    }

    this.frontend.updateState(this.state);
  }

  async fetchBlob(url: string): Promise<Blob> {
    // Maybe this a local file:
    const file = this.localFiles.get(url);
    if (file) {
      return Promise.resolve(file);
    }

    // If not lets try to fetch from network:
    const repsonse = await fetch(url);
    return await repsonse.blob();
  }

  publish() {
  }

  addLocalFile(file: File): string {
    const name = file.name;
    this.localFiles.set(name, file);
    return name;
  }

  ///**
  // * Special case handling of loading a trace from a blob.
  // * This can't be a pure action since we don't want to store
  // * the Blob in the state.
  // */
  //loadTraceFromBlob(blob: Blob): void {
  //  //this.createEngine(blob);
  //  this.doAction(addBlob('[local trace]'));
  //  const id = this.state.newestBlobId;
  //  if (id === null) throw new Error('newestBlobId not set');
  //  this.blobs.set(id, blob);
  //  this.doAction(openTraceFromBlob(id));
  //  this.frontend.updateState(this.state);
  //  this.createEngine(blob);
  //}

  async createEngine(blob: Blob): Promise<Engine> {
    const port = await this.frontend.createWasmEnginePort();
    return WasmEngineProxy.create(port, blob);
  }
}

/**
 * Proxy for talking to the main thread.
 * TODO(hjd): Reduce the boilerplate.
 */
class FrontendProxy {
  private readonly remote: Remote;

  constructor(remote: Remote) {
    this.remote = remote;
  }

  updateState(state: State) {
    return this.remote.send<void>('updateState', [state]);
  }

  createWasmEnginePort() {
    return this.remote.send<MessagePort>('createWasmEnginePort', []);
  }
}

function main() {
  const controller = new Controller();
  forwardRemoteCalls(self as {} as MessagePort, controller);
}

main();
