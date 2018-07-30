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

export const TrackCollectionComponent = {
  oninit() {
    this.collapsed = false;
    this.onHeaderClick = ((e: MouseEvent) => {
                           this.collapsed = !this.collapsed;

                           // TODO: Update height properly via controller.
                           for (const trackState of this.trackStates) {
                             trackState.height = this.collapsed ? 0 : 100;
                           }

                           m.redraw();
                           e.preventDefault();
                         }) as EventListener;
  },
  oncreate(vnode) {
    const el = vnode.dom as HTMLElement;
    const header = el.getElementsByClassName('collection-header')[0];
    header.addEventListener('click', this.onHeaderClick);
  },
  onremove(vnode) {
    const el = vnode.dom as HTMLElement;
    const header = el.getElementsByClassName('collection-header')[0];
    header.removeEventListener('click', this.onHeaderClick);
  },
  view({attrs, children}) {
    this.trackStates = attrs.trackStates;

    return m(
        '.track_collection',
        {
          style: {
            width: '100%',
          }
        },
        m('.collection-header',
          {
            style: {
              width: '100%',
              height: '25px',
              background: 'hsl(213, 22%, 82%)',
              padding: '2px 15px',
              position: 'absolute',
              top: `${attrs.top}px`,
              cursor: 'pointer',
            }
          },
          m('span.collapsedStateIcon',
            {style: {'margin-right': '20px'}},
            this.collapsed ? '>' : 'v'),
          m('span.collection-title', attrs.name)),
        m('.collection-content',
          {style: {display: this.collapsed ? 'none' : 'block'}},
          children));
  }
} as m.Component<{
  name: string,
  top: number,
  trackStates: Array<{height: number}>,
},
                                        {
                                          onHeaderClick: EventListener,
                                          collapsed: boolean,
                                          trackStates: Array<{height: number}>,
                                        }>;
