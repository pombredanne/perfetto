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

export const TRACK_SHELL_WIDTH = 200;


/**
 * Returns yStart for a track relative to canvas top.
 *
 * When the canvas extends above ScrollingTrackDisplay, we have:
 *
 * -------------------------------- canvas
 *   |
 *   |  canvasYStart (negative here)
 *   |
 * -------------------------------- ScrollingTrackDisplay top
 *   |
 *   |  trackYStart (track.attrs.top)
 *   |
 * -------------------------------- track
 *
 * Otherwise, we have:
 *
 * -------------------------------- ScrollingTrackDisplay top
 *   |      |
 *   |      |  canvasYStart (positive here)
 *   |      |
 *   |     ------------------------- ScrollingTrackDisplay top
 *   |
 *   |  trackYStart (track.attrs.top)
 *   |
 * -------------------------------- track
 *
 * In both cases, trackYStartOnCanvas for track is trackYStart - canvasYStart.
 *
 * @param trackYStart Y position of a Track relative to
 * ScrollingTrackDisplay.
 * @param canvasYStart Y position of canvas relative to
 * ScrollingTrackDisplay.
 */
export function getTrackYStartOnCanvas(
    trackYStart: number, canvasYStart: number) {
  return trackYStart - canvasYStart;
}