
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
import {dingus} from '../test/dingus';

import {TrackImpl} from './track_impl';
import {TrackCreator, TrackRegistry} from './track_registry';

// Cannot use dingus on an abstract class.
class MockTrackImpl extends TrackImpl {
  draw() {}
}

function mockTrackCreator(type: string): TrackCreator {
  return {
    type,
    create: () => new MockTrackImpl(dingus<TrackState>(), 0),
  };
}

test('registry returns correct track', () => {
  const registry = new TrackRegistry();

  registry.register(mockTrackCreator('track1'));
  registry.register(mockTrackCreator('track2'));

  expect(registry.getCreator('track1').type).toEqual('track1');
  expect(registry.getCreator('track2').type).toEqual('track2');
});

test('registry throws error on name collision', () => {
  const registry = new TrackRegistry();
  const creator1 = mockTrackCreator('someTrack');
  const creator2 = mockTrackCreator('someTrack');
  registry.register(creator1);
  expect(() => registry.register(creator2)).toThrow();
});

test('registry throws error on non-existent track', () => {
  const registry = new TrackRegistry();
  expect(() => registry.getCreator('nonExistentTrack')).toThrow();
});
