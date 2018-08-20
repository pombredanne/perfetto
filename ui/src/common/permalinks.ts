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

import {State} from './state';

export const BUCKET_NAME = 'perfetto-ui-data';

export async function toSha256(str: string): Promise<string> {
  // TODO(hjd): TypeScript bug with definition of TextEncoder.
  // tslint:disable-next-line no-any
  const buffer = new (TextEncoder as any)('utf-8').encode(str);
  const digest = await crypto.subtle.digest('SHA-256', buffer);
  return Array.from(new Uint8Array(digest)).map(x => x.toString(16)).join('');
}

export async function loadState(id: string): Promise<State> {
  const url = `https://storage.googleapis.com/${BUCKET_NAME}/${id}`;
  const response = await fetch(url);
  const text = await response.text();
  const stateHash = await toSha256(text);
  const state = JSON.parse(text);
  if (stateHash !== id) {
    throw new Error(`State hash does not match ${id} vs. ${stateHash}`);
  }
  return state;
}