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

function readCssVar(prop: string, defaultValue: string): string {
  if (typeof window === 'undefined') {
    return defaultValue;
  } else {
    const body = window.document.body;
    return window.getComputedStyle(body).getPropertyValue(prop);
  }
}

function getTrackShellWidth(): number {
  const width = readCssVar('--track-shell-width', '100px');
  const match = width.match(/^\W(\d+)px$/);
  if (!match) throw Error(`Could not parse shell width as number (${width})`);
  return Number(match[1]);
}

export const TRACK_SHELL_WIDTH = getTrackShellWidth();
export const TRACK_BORDER_COLOR = readCssVar('--track-border-color', '#ffc0cb');
