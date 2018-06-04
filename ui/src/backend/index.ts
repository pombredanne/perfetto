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

import { State, createZeroState } from './state';
import { TraceConfig } from './protos';

let gState: State = createZeroState();

function main() {
  console.log('Hello from the worker!');
  const any_self = (self as any);
  any_self.onmessage = (m: any) => {
    switch (m.data.topic) {
      case 'ping':
        any_self.postMessage({
          topic: 'pong',
        });
        break;
      case 'init':
        gState = m.data.initial_state;
        any_self.postMessage({
          topic: 'new_state',
          new_state: gState,
        });
        break;
      case 'inc':
        gState.counter += 1;
        any_self.postMessage({
          topic: 'new_state',
          new_state: gState,
        });
        break;
      case 'navigate':
        gState.fragment = m.data.fragment;
        any_self.postMessage({
          topic: 'new_state',
          new_state: gState,
        });
        break;
      case 'check':
        gState.checked = m.data.checked;
        any_self.postMessage({
          topic: 'new_state',
          new_state: gState,
        });
        break;
      default:
        return;
    }
  }
}

export {
  main,
};
