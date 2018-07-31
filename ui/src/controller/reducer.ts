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

import {Action} from '../common/actions';
import {State} from '../common/state';

export function rootReducer(state: State, action: Action): State {
  switch (action.type) {
    // TODO(hjd): Make pure.
    case 'INCREMENT':
      state.i++;
      break;
    case 'TRACK_ORDER_SWAP':
      // TODO (michaschwab): Better type for action.
      const orderSwapAction =
          action as {type: string, swapIndex1: number, swapIndex2: number};
      if (!state.tracks[orderSwapAction.swapIndex1] ||
          state.tracks[orderSwapAction.swapIndex2]) {
        break;
      }
      const tempOrder = state.tracks[orderSwapAction.swapIndex1].order;
      state.tracks[orderSwapAction.swapIndex1].order =
          state.tracks[orderSwapAction.swapIndex2].order;
      state.tracks[orderSwapAction.swapIndex2].order = tempOrder;
      break;
    default:
      break;
  }
  return state;
}
