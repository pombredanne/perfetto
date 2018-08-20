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

import {assertExists, assertTrue} from '../base/logging';
import {setPermalink} from '../common/actions';
import {BUCKET_NAME, toSha256} from '../common/permalinks';
import {EngineConfig, State} from '../common/state';

import {Controller} from './controller';
import {globals} from './globals';

// TODO: move common/permalinks into here.

export class PermalinkController extends Controller<'init'> {
  private lastRequestId?: string;
  constructor() {
    super('init');
  }

  run() {
    if (globals.state.permalink.requestId === this.lastRequestId) return;
    const requestId = assertExists(globals.state.permalink.requestId);
    assertTrue(globals.state.permalink.link === undefined);
    this.lastRequestId = requestId;
    PermalinkController.createPermalink().then(url => {
      globals.dispatch(setPermalink(requestId, url));
    });
  }

  private static async createPermalink() {
    const state = {...globals.state};
    state.engines = {...state.engines};
    for (const engine of Object.values<EngineConfig>(state.engines)) {
      // If the trace was opened from a local file, upload it and store the
      // url of the uploaded trace instead.
      if (engine.source instanceof File) {
        const url = await PermalinkController.saveTrace(engine.source);
        engine.source = url;
      }
    }
    const url = await PermalinkController.saveState(state);
    return url;
  }


  private static async saveState(state: State): Promise<string> {
    const text = JSON.stringify(state);
    const name = await toSha256(text);
    const url = 'https://www.googleapis.com/upload/storage/v1/b/' +
        `${BUCKET_NAME}/o?uploadType=media&name=${
                                                  name
                                                }&predefinedAcl=publicRead`;
    const response = await fetch(url, {
      method: 'post',
      headers: {
        'Content-Type': 'application/json; charset=utf-8',
      },
      body: text,
    });
    await response.json();

    return `${self.location.origin}#!/?s=${name}`;
  }

  private static async saveTrace(trace: File): Promise<string> {
    // TODO(hjd): This should probably also be a hash but that requires
    // trace processor support.
    const name = uuidv4();
    const url = 'https://www.googleapis.com/upload/storage/v1/b/' +
        `${BUCKET_NAME}/o?uploadType=media&name=${
                                                  name
                                                }&predefinedAcl=publicRead`;
    const response = await fetch(url, {
      method: 'post',
      headers: {
        'Content-Type': 'application/octet-stream;',
      },
      body: trace,
    });
    await response.json();
    return `https://storage.googleapis.com/${BUCKET_NAME}/${name}`;
  }
}
