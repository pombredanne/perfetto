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

import {TrackState} from '../common/state';
import {TrackImpl} from './track_impl';

/**
 * This interface forces track implementations to have two static properties:
 * type and a create function.
 *
 * Typescript does not have abstract static members, which is why this needs to
 * be in a seperate interface. We need the |create| method because the stored
 * value in the registry is an abstract class, and we cannot call 'new'
 * on an abstract class.
 */
export interface TrackCreator<T = TrackImpl> {
  // Store the type explicitly as a string as opposed to using class.name in
  // case we ever minify our code.
  readonly type: string;

  create(TrackState: TrackState, width: number): T;
}

/**
 * Track type to track creator registry. Throws error on name collision.
 */
export class TrackRegistry {
  private registry: Map<string, TrackCreator>;

  constructor() {
    this.registry = new Map<string, TrackCreator>();
  }

  register(creator: TrackCreator) {
    const trackType = creator.type;
    if (this.registry.has(trackType)) {
      throw new Error(`TrackType ${trackType} already exists in the registry`);
    }
    this.registry.set(trackType, creator);
  }

  getCreator(trackType: string): TrackCreator {
    const creator = this.registry.get(trackType);
    if (creator === undefined) {
      throw new Error(`No creator for ${trackType} has been registered yet.`);
    }
    return creator;
  }
}