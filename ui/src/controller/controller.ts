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

import {ObjectById} from '../common/state';

interface HasId {
  id: string;
}

interface Controller<Config> {
  update(_: Config): void;
  destroy(): void;
}

export class CreateUpdateDestroy<
    Config extends HasId, ConfigController extends Controller<Config>> {
  private readonly factory: (config: Config) => ConfigController | null;
  private readonly controllers: Map<string, ConfigController>;

  constructor(factory: (config: Config) => ConfigController | null) {
    this.factory = factory;
    this.controllers = new Map();
  }

  update(configs: ObjectById<Config>) {
    const ids = new Set([...Object.keys(configs), ...this.controllers.keys()]);
    for (const id of ids) {
      const config = configs[id];
      const controller = this.controllers.get(id);
      if (controller && config) {
        controller.update(config);
      } else if (config) {
        const newController = this.factory(config);
        if (newController) {
          this.controllers.set(id, newController);
        }
      } else if (controller) {
        controller.destroy();
      }
    }
  }
}
