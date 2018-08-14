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

import {dingus} from '../test/dingus';
import {CreateUpdateDestroy} from './controller';

interface Config {
  id: string;
  value: string;
}

interface Controller {
  update(_: Config): void;
  destroy(): void;
}

type Factory = (_: Config) => Controller;

test('crud with no configs', () => {
  const factory = dingus<Factory>();
  const crud = new CreateUpdateDestroy<Config, Controller>(factory);
  crud.update({});
  expect(factory.calls.length).toBe(0);
});

test('crud with one config', () => {
  const factory = dingus<Factory>();
  const crud = new CreateUpdateDestroy<Config, Controller>(factory);
  crud.update({'a': {'id': 'a', 'value': 'A'}});
  expect(factory.calls.length).toBe(1);

  crud.update({'a': {'id': 'a', 'value': 'A'}});
  expect(factory.calls.length).toBe(1);
  expect(factory().update.calls.length).toBe(1);

  crud.update({});
  expect(factory().update.calls.length).toBe(1);
  expect(factory().destroy.calls.length).toBe(1);
});
