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

import {DraftObject, produce} from 'immer';

import {
  createEmptyState,
  defaultTraceTime,
  State,
  Status,
  TraceTime
} from './state';
import {TimeSpan} from './time';

export interface Action { type: string; }

export function openTraceFromUrl(url: string) {
  return {
    type: 'openTraceFromUrl',
    url,
  };
}

export function openTraceFromFile(file: File) {
  return {
    type: 'openTraceFromFile',
    file,
  };
}

// TODO(hjd): Remove CPU and add a generic way to handle track specific state.
export function addTrack(
    engineId: string, trackKind: string, name: string, config: {}) {
  return {
    type: 'addTrack',
    engineId,
    kind: trackKind,
    name,
    config,
  };
}

export function requestTrackData(
    trackId: string, start: number, end: number, resolution: number) {
  return {type: 'reqTrackData', trackId, start, end, resolution};
}

export function clearTrackDataRequest(trackId: string) {
  return {type: 'clearTrackDataReq', trackId};
}

export function deleteQuery(queryId: string) {
  return {
    type: 'deleteQuery',
    queryId,
  };
}

export function navigate(route: string) {
  return {
    type: 'navigate',
    route,
  };
}

export function moveTrack(trackId: string, direction: 'up'|'down') {
  return {
    type: 'moveTrack',
    trackId,
    direction,
  };
}

export function toggleTrackPinned(trackId: string) {
  return {
    type: 'toggleTrackPinned',
    trackId,
  };
}

export function setEngineReady(engineId: string, ready = true) {
  return {type: 'setEngineReady', engineId, ready};
}

export function createPermalink() {
  return {type: 'createPermalink', requestId: new Date().toISOString()};
}

export function setPermalink(requestId: string, hash: string) {
  return {type: 'setPermalink', requestId, hash};
}

export function loadPermalink(hash: string) {
  return {type: 'loadPermalink', requestId: new Date().toISOString(), hash};
}

export function setState(newState: State) {
  return {
    type: 'setState',
    newState,
  };
}

export function setTraceTime(ts: TimeSpan) {
  return {
    type: 'setTraceTime',
    startSec: ts.start,
    endSec: ts.end,
    lastUpdate: Date.now() / 1000,
  };
}

export function setVisibleTraceTime(ts: TimeSpan) {
  return {
    type: 'setVisibleTraceTime',
    startSec: ts.start,
    endSec: ts.end,
    lastUpdate: Date.now() / 1000,
  };
}

export function updateStatus(msg: string) {
  return {type: 'updateStatus', msg, timestamp: Date.now() / 1000};
}

type StateDraft = DraftObject<State>;

export class Model {
  private _state: State;

  constructor() {
    this._state = createEmptyState();
  }

  get state(): State {
    return this._state;
  }

  setStateForTesting(state: State) {
    this._state = state;
  }

  // tslint:disable-next-line no-any
  doAction(action: any): void {
    if (action.type === 'setState') {
      this.setState(action.newState);
    }
    this._state = produce(this.state, draft => {
      (DoActions as any)[action.type](draft, action);
    });
  }

  setState(args: {newState: State}): void {
    this._state = args.newState;
  }
}

export const DoActions = {

  navigate(draft: StateDraft, args: {route: string}): void {
  draft.route = args.route;
  },

  openTraceFromFile(draft: StateDraft, args: {file: File}): void {
  draft.traceTime = {...defaultTraceTime};
  draft.visibleTraceTime = {...defaultTraceTime};
  const id = `${draft.nextId++}`;
  // Reset displayed tracks.
  draft.pinnedTracks = [];
  draft.scrollingTracks = [];
  draft.engines[id] = {
    id,
    ready: false,
    source: args.file,
  };
  draft.route = `/viewer`;
  },

  openTraceFromUrl(draft: StateDraft, args: {url: string}): void {
  draft.traceTime = {...defaultTraceTime};
  draft.visibleTraceTime = {...defaultTraceTime};
  const id = `${draft.nextId++}`;
  // Reset displayed tracks.
  draft.pinnedTracks = [];
  draft.scrollingTracks = [];
  draft.engines[id] = {
    id,
    ready: false,
    source: args.url,
  };
  draft.route = `/viewer`;
  },

  addTrack(draft: StateDraft, args: {
    engineId: string;
    kind: string;
    name: string;
    config: {};
  }): void {
  const id = `${draft.nextId++}`;
  draft.tracks[id] = {
    id,
    engineId: args.engineId,
    kind: args.kind,
    name: args.name,
    config: args.config,
  };
  draft.scrollingTracks.push(id);
  },

  reqTrackData(draft: StateDraft, args: {
  trackId: string;
  start: number;
  end: number;
  resolution: number;
  }): void {
  const id = args.trackId;
  draft.tracks[id].dataReq = {
    start: args.start,
    end: args.end,
    resolution: args.resolution
  };
  },

  clearTrackDataReq(draft: StateDraft, args: {trackId: string}): void {
  const id = args.trackId;
  draft.tracks[id].dataReq = undefined;
  },

  executeQuery(draft: StateDraft, args: {queryId: string; engineId: string; query: string}): void {
  draft.queries[args.queryId] = {
    id: args.queryId,
    engineId: args.engineId,
    query: args.query,
  };
  },

  deleteQuery(draft: StateDraft, args: {queryId: string}): void {
  delete draft.queries[args.queryId];
  },

  moveTrack(draft: StateDraft, args: {
    trackId: string
    direction: 'up'|'down',
  }): void {

  const id = args.trackId;
  const isPinned = draft.pinnedTracks.includes(id);
  const isScrolling = draft.scrollingTracks.includes(id);
  if (!isScrolling && !isPinned) {
    throw new Error(`No track with id ${id}`);
  }
  const tracks = isPinned ? draft.pinnedTracks : draft.scrollingTracks;

  const oldIndex = tracks.indexOf(id);
  const newIndex = args.direction === 'up' ? oldIndex - 1 : oldIndex + 1;
  const swappedTrackId = tracks[newIndex];
  if (isPinned && newIndex === draft.pinnedTracks.length) {
    // Move from last element of pinned to first element of scrolling.
    draft.scrollingTracks.unshift(draft.pinnedTracks.pop()!);
  } else if (isScrolling && newIndex === -1) {
    // Move first element of scrolling to last element of pinned.
    draft.pinnedTracks.push(draft.scrollingTracks.shift()!);
  } else if (swappedTrackId) {
    tracks[newIndex] = id;
    tracks[oldIndex] = swappedTrackId;
  }
  },

  toggleTrackPinned(draft: StateDraft, args: {
    trackId: string
  }): void {
  const id = args.trackId;
  const isPinned = draft.pinnedTracks.includes(id);

  if (isPinned) {
    draft.pinnedTracks.splice(draft.pinnedTracks.indexOf(id), 1);
    draft.scrollingTracks.unshift(id);
  } else {
    draft.scrollingTracks.splice(draft.scrollingTracks.indexOf(id), 1);
    draft.pinnedTracks.push(id);
  }
  },

  setEngineReady(draft: StateDraft, args: {engineId: string, ready: boolean}): void {
  draft.engines[args.engineId].ready = args.ready;
  },

  createPermalink(draft: StateDraft, args: {requestId: string}): void {
  draft.permalink = {requestId: args.requestId, hash: undefined};
  },

  setPermalink(draft: StateDraft, args: {requestId: string, hash: string}): void {
  // Drop any links for old requests.
  if (draft.permalink.requestId !== args.requestId) return;
  draft.permalink = args;
  },

  loadPermalink(draft: StateDraft, args: {requestId: string, hash: string}): void {
  draft.permalink = args;
  },

  setTraceTime(draft: StateDraft, args: TraceTime): void {
  draft.traceTime = args;
  },

  setVisibleTraceTime(draft: StateDraft, args: TraceTime): void {
  draft.visibleTraceTime = args;
  },

  updateStatus(draft: StateDraft, args: Status): void {
  draft.status = args;
  },
}

interface DeferredAction<Args> {
      type: string;
      args: Args;
    }

    type ActionFunction<Args> = (draft: StateDraft, args: Args) => void;
    type DeferredActionFunc<T> = T extends ActionFunction<infer Args>?
        (args: Args) => DeferredAction<Args>:
        never;

    type DeferredActions<C> = {
      [P in keyof C]: DeferredActionFunc<C[P]>;
    }

    export const Actions =
        new Proxy<DeferredActions<typeof DoActions>>({} as any, {
          get(_: any, prop: string, _2: any) {
            return (args: {}): DeferredAction<{}> => {
              return {
                type: prop,
                args,
              };
            };
          },
        });
