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

import {search, searchSegment} from './binary_search';

test('binarySearch', () => {
  expect(search(Float64Array.of(), 100)).toEqual(-1);
  expect(search(Float64Array.of(42), 42)).toEqual(0);
  expect(search(Float64Array.of(42), 43)).toEqual(0);
  expect(search(Float64Array.of(42), 41)).toEqual(-1);
  expect(search(Float64Array.of(42, 43), 42)).toEqual(0);
  expect(search(Float64Array.of(42, 43), 43)).toEqual(1);
  expect(search(Float64Array.of(42, 43), 44)).toEqual(1);
  expect(search(Float64Array.of(42, 43, 44), 41)).toEqual(-1);
  expect(search(Float64Array.of(42, 43, 44), 42)).toEqual(0);
  expect(search(Float64Array.of(42, 43, 44), 43)).toEqual(1);
  expect(search(Float64Array.of(42, 43, 44), 44)).toEqual(2);
  expect(search(Float64Array.of(42, 43, 44), 45)).toEqual(2);
});

test('searchSegment', () => {
  expect(searchSegment(Float64Array.of(), 100)).toEqual([-1, -1]);

  expect(searchSegment(Float64Array.of(42), 41)).toEqual([-1, 0]);
  expect(searchSegment(Float64Array.of(42), 42)).toEqual([0, -1]);
  expect(searchSegment(Float64Array.of(42), 43)).toEqual([0, -1]);

  expect(searchSegment(Float64Array.of(42, 44), 42)).toEqual([0, 1]);
});
