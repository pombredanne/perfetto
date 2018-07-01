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

import { createMockClass } from '../test/mock_class';

test('can mock from prototype', async () => {

  const map = createMockClass<Map<number,number>>(Map.prototype);

  map.set(123, 456);
  expect(map.set.mock.calls[0]).toEqual([123, 456]);
});

test('can mock from list of methods', async () => {

  const ctx = createMockClass<CanvasRenderingContext2D>(['lineTo']);

  ctx.lineTo(123, 456);
  expect(ctx.lineTo.mock.calls[0]).toEqual([123, 456]);
});
