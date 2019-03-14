// Copyright (C) 2019 The Android Open Source Project
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

export function translateState(state: string|undefined) {
  let result = '';
  if (state === undefined || state.length > 2) return result;
  switch (state[0]) {
    case 'R':
      result = 'Runnable';
      break;
    case 'S':
      result = 'Interruptable Sleep';
      break;
    case 'D':
      result = 'Uninterruptible (Disk) Sleep';
      break;
    case 'T':
      result = 'Stopped';
      break;
    case 't':
      result = 'Traced';
      break;
    case 'X':
      result = 'Exit (dead)';
      break;
    case 'Z':
      result = 'Exit (zombie)';
      break;
    case 'x':
      result = 'Task Dead';
      break;
    case 'K':
      result = 'Wake Kill';
      break;
    case 'W':
      result = 'Waking';
      break;
    case 'P':
      result = 'Parked';
      break;
    case 'N':
      result = 'No Load';
      break;
    case 'n':
      result = 'New Task';
      break;
    default:
      return state;
  }
  if (state[1] === '+') {
    result += ' (preempted)';
  }
  return result;
}
