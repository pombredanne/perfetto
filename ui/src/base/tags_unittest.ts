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

import {TagCollection, Tags} from './tags';

interface Fruit {
  tags: Tags;
}

const apple: Fruit = {
  tags: {
    color: 'green',
    shape: 'sphere',
  },
};

const tomato: Fruit = {
  tags: {
    color: 'red',
    shape: 'sphere',
  },
};

const banana: Fruit = {
  tags: {
    color: 'yellow',
  },
};

test('asArray', () => {
  const tags = TagCollection.from<Fruit>([apple]);
  expect(tags.asArray()).toContain(apple);
});

test('filter', () => {
  const tags = TagCollection.from<Fruit>([apple, tomato, banana]);
  expect(tags.filter('color', 'yellow').asArray()).toEqual([banana]);
  expect(tags.filter('shape').asArray()).toEqual([apple, tomato]);
});

test('filter', () => {
  const tags = TagCollection.from<Fruit>([apple, tomato, banana]);
  expect(tags.sort(['color']).asArray()).toEqual([apple, tomato, banana]);
});
