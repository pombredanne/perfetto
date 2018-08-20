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
import {Remote} from '../base/remote';
import {Action} from '../common/actions';
import {createEmptyState, State} from '../common/state';

import {
  ControllerAny,
  runControllerTree,
} from './controller';
import {Engine} from './engine';
import {rootReducer} from './reducer';
import {WasmEngineProxy} from './wasm_engine_proxy';

/**
 * Global accessors for state/dispatch in the controller.
 */
class Globals {
  private _state?: State;
  private _rootController?: ControllerAny;
  private _frontend?: Remote;
  private _runningControllers = false;
  private _didPostControllersRun = false;
  private _didPostStateUpdate = false;

  initialize(rootController: ControllerAny, frontendProxy: Remote) {
    this._state = createEmptyState();
    this._rootController = rootController;
    this._frontend = frontendProxy;
  }

  dispatch(action: Action): void {
    this.dispatchMultiple([action]);
  }

  dispatchMultiple(actions: Action[]): void {
    for (const action of actions) {
      this._state = rootReducer(this.state, action);
    }

    this.runControllers();

    if (this._didPostStateUpdate) return;
    this._didPostStateUpdate = true;
    setTimeout(() => {
      this._didPostStateUpdate = false;
      assertExists(this._frontend).send<void>('updateState', [this.state]);
    }, 32);
  }

  runControllers() {
    if (this._runningControllers) {
      if (this._didPostControllersRun) return;
      this._didPostControllersRun = true;
      setTimeout(() => {
        this._didPostControllersRun = false;
        this.runControllers();
      });
      return;
    }

    this._runningControllers = true;
    runControllerTree(assertExists(this._rootController));
    this._runningControllers = false;
  }

  async createEngine(blob: Blob): Promise<Engine> {
    const port = await assertExists(this._frontend)
                     .send<MessagePort>('createWasmEnginePort', []);
    return WasmEngineProxy.create(port, blob);
  }

  // TODO: this needs to be cleaned up.
  publish(what: 'OverviewData'|'TrackData'|'Threads'|'QueryResult', data: {}) {
    assertExists(this._frontend)
        .send<void>(`publish${what}`, [data])
        .then(() => {});
  }

  get state(): State {
    return assertExists(this._state);
  }

  set state(state: State) {
    this._state = state;
  }

  resetForTesting() {
    this._state = undefined;
    this._rootController = undefined;
  }
}

export const globals = new Globals();
