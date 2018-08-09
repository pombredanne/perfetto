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

import * as uuidv4 from 'uuid/v4';

/**
 * A plain js object, holding objects of type |Class| keyed by string id.
 * We use this instead of using |Map| object since it is simpler and faster to
 * serialize for use in postMessage.
 */
export interface ObjectById<Class extends{id: string}> { [id: string]: Class; }

export interface TrackState {
  id: string;
  engineId: string;
  height: number;
  kind: string;
  name: string;
  // TODO(hjd): This needs to be nested into track kind specific state.
  cpu: number;
}

export interface EngineConfig {
  id: string;
  ready: boolean;
  source: string|File;
}

export interface QueryConfig {
  id: string;
  engineId: string;
  query: string;
}

export interface PermalinkConfig {
  id: string;
  state: State;
}

export interface State {
  route: string|null;
  nextId: number;

  /**
   * Open traces.
   */
  engines: ObjectById<EngineConfig>;
  tracks: ObjectById<TrackState>;
  displayedTrackIds: string[];
  queries: ObjectById<QueryConfig>;
  permalinks: ObjectById<PermalinkConfig>;
}

export function createEmptyState(): State {
  return {
    route: null,
    nextId: 0,
    tracks: {},
    displayedTrackIds: [],
    engines: {},
    queries: {},
    permalinks: {},
  };
}

async function toSha256(str: string): Promise<string> {
  // TODO(hjd): TypeScript bug with definition of TextEncoder.
  // tslint:disable-next-line no-any
  const buffer = new (TextEncoder as any)('utf-8').encode(str);
  const digest = await crypto.subtle.digest('SHA-256', buffer);
  return Array.from(new Uint8Array(digest)).map(x => x.toString(16)).join('');
}

export async function saveState(state: State): Promise<string> {
  const text = JSON.stringify(state);
  const bucketName = 'perfetto-ui-data';
  const name = await toSha256(text);
  const url = 'https://www.googleapis.com/upload/storage/v1/b/' +
      `${bucketName}/o?uploadType=media&name=${name}`;
  const response = await fetch(url, {
    method: 'post',
    headers: {
      'Content-Type': 'application/json; charset=utf-8',
    },
    body: text,
  });
  await response.json();

  // return `${self.location.origin}/?s=${name}`;
  return `${self.location.origin}/index.html#!/?s=${name}`;
}

export async function loadState(id: string): Promise<State> {
  const url = `https://storage.googleapis.com/perfetto-ui-data/${id}`;
  const response = await fetch(url);
  return response.json();
}

export async function saveTrace(trace: File): Promise<string> {
  const bucketName = 'perfetto-ui-data';
  const name = uuidv4();
  const url = 'https://www.googleapis.com/upload/storage/v1/b/' +
      `${bucketName}/o?uploadType=media&name=${name}`;
  const response = await fetch(url, {
    method: 'post',
    headers: {
      'Content-Type': 'application/octet-stream;',
    },
    body: trace,
  });
  const json = await response.json();
  console.log(json);
  return `https://storage.googleapis.com/perfetto-ui-data/${name}`;
}
