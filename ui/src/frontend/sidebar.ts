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

import {Action, navigate} from '../common/actions';

import {quietDispatch} from './mithril_helpers';

const navigateHome = navigate('/');
const navigateViewer = navigate('/viewer');
const expanded = [true, false, false, false];

function navlink(iconName: string, text: string, action: Action): m.Children {
  return m(
      'li',
      m('a[href=/]',
        {
          onclick: quietDispatch(e => {
            e.preventDefault();
            return action;
          }),
        },
        m('i.material-icons', iconName) text));
}

function sectionHeader(
    title: string, subtitle: string, sectionId: number): m.Children {
  return m(
      '.section-header',
      {onclick: () => expanded[sectionId] = !expanded[sectionId]},
      m('h1', title),
      m('h2', subtitle));
}

export const Sidebar: m.Component = {
  view() {
    console.log('view', expanded);
    return m(
        'nav.sidebar',
        m('header', 'Perfetto'),
        m(`section${expanded[0] ? '.expanded' : ''}`,
          sectionHeader('Traces', 'Open or record a trace', 0),
          m('.section-content',
            m('ul',
              navlink('folder_open', 'Open trace file', navigateHome),
              navlink('art_track', 'View Trace', navigateViewer),
              navlink('fiber_smart_record', 'Record new trace', navigateHome),
              navlink('share', 'Share current trace', navigateHome)))),
        m(`section${expanded[1] ? '.expanded' : ''}`,
          sectionHeader('Workspaces', 'Custom and pre-arranged views', 1),
          m('.section-content',
            m('ul',
              navlink('art_track', 'Big picture', navigateHome),
              navlink('apps', 'Apps and processes', navigateHome),
              navlink('storage', 'Storage and I/O', navigateHome),
              navlink('library_add', 'Add custom...', navigateHome)))),
        m(`section${expanded[2] ? '.expanded' : ''}`,
          sectionHeader(
              'Tracks and Views', 'Add new tracks to the workspace', 2),
          m('.section-content',
            m('ul',
              navlink('touch_app', 'User interactions', navigateHome),
              navlink(
                  'perm_device_information', 'User interactions', navigateHome),
              navlink('blur_linear', 'Scheduler trace', navigateHome),
              navlink('equalizer', 'Process list', navigateHome),
              navlink('battery_alert', 'Battary & power', navigateHome)))),
        m(`section${expanded[3] ? '.expanded' : ''}`,
          sectionHeader(
              'Metrics and Auditors', 'Summary analysis of the trace', 3),
          m('.section-content',
            m('ul',
              navlink('table_chart', 'CPU usage breakdown ', navigateHome),
              navlink('memory', 'Memory breakdown', navigateHome)))));
  }
};
