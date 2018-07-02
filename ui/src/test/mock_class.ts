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

import Mock = jest.Mock;

export type MockClass<T> = {
  [P in keyof T]: Mock<T>;
};

export function createMockClass<T>(): MockClass<T> & T {
  const mockFcts: {[prop: string]: Mock<T>} = {};
  return new Proxy({}, {
    get: (_, propKey: string) => {
      if(!mockFcts[propKey]) {
        mockFcts[propKey] = jest.fn<T>();
      }
      return mockFcts[propKey];
    }
  }) as MockClass<T> & T;
}
