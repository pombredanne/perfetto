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

interface SidebarItem {
  t: string;
  l: string;
  i: string;
}

interface SidebarSection {
  title: string;
  summary: string;
  expanded?: boolean;
  items: SidebarItem[];
}

export const Sidebar: m.Component<{}, {sections: SidebarSection[]}> = {
  oninit() {
    this.sections = [
      {
        title: 'Traces',
        expanded: true,
        summary: 'Open or record a trace',
        items: [
          {t: 'Open trace file', l: '/', i: 'folder_open'},
          {t: 'Open example trace', l: '/', i: 'description'},
          {t: 'View trace', l: '/viewer', i: 'art_track'},
          {t: 'Record new trace', l: '/record', i: 'fiber_smart_record'},
          {t: 'Share current trace', l: '/record', i: 'share'},
        ],
      },
      {
        title: 'Workspaces',
        summary: 'Custom and pre-arranged views',
        items: [
          {t: 'Big Picture', l: '/', i: 'art_track'},
          {t: 'Apps and process', l: '/', i: 'apps'},
          {t: 'Storage and I/O', l: '/', i: 'storage'},
          {t: 'Add custom...', l: '/', i: 'library_add'},
        ],
      },
      {
        title: 'Tracks and views',
        summary: 'Add new tracks to the workspace',
        items: [
          {t: 'User interactions', l: '/', i: 'touch_app'},
          {t: 'Device info', l: '/', i: 'perm_device_information'},
          {t: 'Scheduler trace', l: '/', i: 'blur_linear'},
          {t: 'Process list', l: '/', i: 'equalizer'},
          {t: 'Battery and power', l: '/', i: 'battery_alert'},
        ],
      },
      {
        title: 'Metrics and auditors',
        summary: 'Add new tracks to the workspace',
        items: [
          {t: 'CPU Usage breakdown', l: '/', i: 'table_chart'},
          {t: 'Memory breakdown', l: '/', i: 'memory'},
        ],
      },
    ];
  },
  view() {
    const vdomSections = [];
    for (const section of this.sections) {
      const vdomItems = [];
      for (const item of section.items) {
        vdomItems.push(
            m('li',
              m(`a[href=${item.l}]`,
                {oncreate: m.route.link},
                m('i.material-icons', item.i),
                item.t)));
      }
      vdomSections.push(
          m(`section${section.expanded ? '.expanded' : ''}`,
            {onclick: () => section.expanded = !section.expanded},
            m('h1', section.title),
            m('h2', section.summary),
            m('.section-content', m('ul', vdomItems))));
    }
    return m('nav.sidebar', m('header', 'Perfetto'), ...vdomSections);
  },
  oncreate(vnode) {
    // In lieu of a link on the <li> element, prevent that the click propagates
    // up to the <section> triggering its compression.
    const uls = vnode.dom.querySelectorAll('ul');
    for (let i = 0; i < uls.length; i++) {
      const ul = uls[i];
      (ul as HTMLElement).addEventListener('click', (e) => {
        e.stopImmediatePropagation();
      });
    }
  }
};