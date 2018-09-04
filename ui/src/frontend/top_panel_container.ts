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

import {OverviewTimelinePanel} from './overview_timeline_panel';
import {Panel} from './panel';
// import {TimeAxisPanel} from './time_axis_panel';

import {PanelContainer} from './panel_container';

export const TopPanelContainer = {
  view() {
    return m('.pinned-panel-container', m(PanelContainer, {
               doesScroll: false,
               panels: [
                 m(OverviewTimelinePanel),
               ]
             }, ));
  },
} as m.Component<{}, {panels: Panel[]}>;
