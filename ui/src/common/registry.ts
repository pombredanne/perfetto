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

export interface HasType { type: string; }

export class Registry<T extends HasType> {
  private registry: Map<string, T>;

  constructor() {
    this.registry = new Map<string, T>();
  }

  register(registrent: T) {
    const type = registrent.type;
    if (this.registry.has(type)) {
      throw new Error(`TrackType ${type} already exists in the registry`);
    }
    this.registry.set(type, registrent);
  }

  get(type: string): T {
    const registrent = this.registry.get(type);
    if (registrent === undefined) {
      throw new Error(`No creator for ${type} has been registered yet.`);
    }
    return registrent;
  }

  unregisterAllForTesting(): void {
    this.registry.clear();
  }
}
