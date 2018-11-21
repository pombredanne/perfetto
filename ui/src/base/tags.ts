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

export interface Tags { [key: string]: string|number; }

interface TaggedThing {
  tags: Tags;
}

export class TagCollection<T extends TaggedThing> {
  static from<T extends TaggedThing>(things: Array<T>): TagCollection<T> {
    return new TagCollection(things);
  }

  private things: Array<T>;

  private constructor(things: Array<T>) {
    this.things = things;
  }

  filter(key: string, expectedValue?: string|number): TagCollection<T> {
    const filtered = this.things.filter(thing => {
      const actualValue = thing.tags[key];
      if (actualValue === undefined) return false;
      if (expectedValue === undefined) return true;
      return expectedValue === actualValue;
    });
    return new TagCollection(filtered);
  }

  sort(keys: string[]): TagCollection<T> {
    const extractKey = (thing: T) => {
      return keys.map(key => thing.tags[key]);
    };

    const sorted = this.things.sort((a, b) => {
      if (extractKey(a) < extractKey(b)) return -1;
      if (extractKey(a) > extractKey(b)) return 1;
      return 0;
    });
    return new TagCollection(sorted);
  }

  asArray(): T[] {
    return this.things;
  }
}
