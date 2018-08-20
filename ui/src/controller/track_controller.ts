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
import {clearTrackDataRequest} from '../common/actions';
import {Registry} from '../common/registry';
import {Controller} from './controller';
import {ControllerFactory} from './controller';
import {Engine} from './engine';
import {globals} from './globals';

type States = 'init';

export interface TrackControllerArgs {
  trackId: string;
  engine: Engine;
}

export interface TrackControllerFactory extends
    ControllerFactory<TrackControllerArgs> {
  kind: string;
}

export const trackControllerRegistry = new Registry<TrackControllerFactory>();

export abstract class TrackController extends Controller<States> {
  readonly trackId: string;
  readonly engine: Engine;

  constructor(args: TrackControllerArgs) {
    super('init');
    this.trackId = args.trackId;
    this.engine = args.engine;
  }

  abstract onBoundsChange(start: number, end: number, resolution: number): void;

  get trackState() {
    return assertExists(globals.state.tracks[this.trackId]);
  }

  run() {
    if (this.trackState.reqTimeStart === undefined ||
        this.trackState.reqTimeEnd === undefined ||
        this.trackState.reqTimeRes === undefined) {
      return;
    }
    const {reqTimeStart, reqTimeEnd, reqTimeRes} = this.trackState;
    globals.dispatch(clearTrackDataRequest(this.trackId));
    this.onBoundsChange(reqTimeStart, reqTimeEnd, reqTimeRes);
  }
}
