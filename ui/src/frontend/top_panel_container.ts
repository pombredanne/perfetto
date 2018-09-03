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

import * as m from 'mithril';

import {globals} from './globals';
import {OverviewTimelinePanel} from './overview_timeline_panel';
import {Panel} from './panel';
import {PanelContainer, TrackPanelById} from './panel_container';
import {TimeAxisPanel} from './time_axis_panel';

export const TopPanelContainer = {
  oninit() {
    this.constantPanels = [];
    this.pinnedPanels = [];
    this.trackPanelById = new TrackPanelById();
  },

  view() {
    if (this.constantPanels.length === 0) {
      this.constantPanels.push(new OverviewTimelinePanel());
      this.constantPanels.push(new TimeAxisPanel());
    }

    const displayedTrackIds = globals.state.pinnedTracks;
    this.trackPanelById.clearObsoleteTracks(displayedTrackIds);
    const panels = this.constantPanels.slice();
    for (const id of displayedTrackIds) {
      const trackState = globals.state.tracks[id];
      const trackPanel = this.trackPanelById.getOrCreateTrack(trackState);
      panels.push(trackPanel);
    }

    return m(
        '.pinned-panel-container',
        m(PanelContainer, {panels, doesScroll: false}));
  },
} as m.Component<{}, {
  constantPanels: Panel[],
  pinnedPanels: Panel[],
  trackPanelById: TrackPanelById,
}>;
