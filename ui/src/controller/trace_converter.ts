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

import * as trace_to_text from '../gen/trace_to_text';

export function ConvertTrace() {
  const mod = trace_to_text({
    noInitialRun: true,
    locateFile: (s: string) => s,
    print: (x) => console.log('P', x),
    printErr: (x) => console.warn('E', x),
    onRuntimeInitialized: () => {},
    onAbort: () => {
      console.log('ABORT');
    },
  });
  mod.FS.mkdir('/fs');
  mod.FS.mount(mod.FS.filesystems.WORKERFS, {blobs: []}, '/fs');

  return mod;
}
