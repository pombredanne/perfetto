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

//import {Action} from '../common/actions';
import {State} from '../common/state';

//function getDemoTracks(): ObjectById<TrackState> {
//  const tracks: {[key: string]: TrackState;} = {};
//  for (let i = 0; i < 10; i++) {
//    let trackType;
//    // The track type strings here are temporary. They will be supplied by the
//    // controller side track implementation.
//    if (i % 2 === 0) {
//      trackType = 'CpuSliceTrack';
//    } else {
//      trackType = 'CpuCounterTrack';
//    }
//    tracks[i] = {
//      id: i.toString(),
//      type: trackType,
//      height: 100,
//      name: `Track ${i}`,
//    };
//  }
//  return tracks;
//}

export function rootReducer(state: State, action: any): State {
  switch (action.type) {
    case 'NAVIGATE': {
      const nextState = {...state};
      nextState.route = action.route;
      return nextState;
    }

    case 'OPEN_TRACE': {
      const nextState = {...state};
      nextState.engines = {...state.engines};

      const id = '' + nextState.nextId++;
      nextState.engines[id] = {
        id,
        url: action.url,
      };
      nextState.route = `/query/${id}`;

      return nextState;
    }

    case 'ADD_TRACK': {
      const nextState = {...state};
      nextState.tracks = {...state.tracks};
      const id = '' + nextState.nextId++;
      nextState.tracks[id] = {
        id,
        engineId: action.engineId,
        kind: action.trackKind,
        name: 'Cpu Track',
        height: 100,
      };
      return nextState;
    }

    default:
      break;
  }
  return state;
}
