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

interface OmniboxState {
  selResult: number;
  numResults: number;
  mode: 'search'|'command';
}

function clearOmniboxResults() {
  // TODO(primiano): Implement in next CLs.
}

function onKeyDown(dom: HTMLInputElement, state: OmniboxState, e: Event) {
  e.stopPropagation();
  const key = (e as KeyboardEvent).key;

  // Avoid that the global 'a', 'd', 'w', 's' handler sees these keystrokes.
  // TODO: this seems a bug in the pan_and_zoom_handler.ts.
  if (key === 'ArrowUp' || key === 'ArrowDown') {
    e.preventDefault();
    return;
  }
  const txt = dom.querySelector('input') as HTMLInputElement;
  if (key === ':' && txt.value === '') {
    state.mode = 'command';
    m.redraw();
    e.preventDefault();
    return;
  }
  if (key === 'Escape' && state.mode === 'command') {
    txt.value = '';
    state.mode = 'search';
    m.redraw();
    return;
  }
  if (key === 'Backspace' && txt.value.length === 0 &&
      state.mode === 'command') {
    state.mode = 'search';
    m.redraw();
    return;
  }
  // TODO(primiano): add query handling here.
}

function onKeyUp(state: OmniboxState, e: Event) {
  e.stopPropagation();
  const key = (e as KeyboardEvent).key;
  const txt = e.target as HTMLInputElement;
  if (key === 'ArrowUp' || key === 'ArrowDown') {
    state.selResult += (key === 'ArrowUp') ? -1 : 1;
    state.selResult = Math.max(state.selResult, 0);
    state.selResult = Math.min(state.selResult, state.numResults - 1);
    e.preventDefault();
    m.redraw();
    return;
  }
  if (txt.value.length <= 0 || key === 'Escape') {
    clearOmniboxResults();
    m.redraw();
    return;
  }
  // TODO(primiano): add query handling here.
}

const Omnibox: m.Component<{}, OmniboxState> = {
  oninit(vnode) {
    vnode.state = {selResult: 0, numResults: 0, mode: 'search'};
  },
  oncreate(vnode) {
    const txt = vnode.dom.querySelector('input') as HTMLInputElement;
    txt.addEventListener('blur', clearOmniboxResults);
    txt.addEventListener(
        'keydown', onKeyDown.bind(undefined, vnode.dom, vnode.state));
    txt.addEventListener('keyup', onKeyUp.bind(undefined, vnode.state));
  },
  view(vnode) {
    // TODO(primiano): handle query results here.
    const placeholder = {
      search: 'Search or type : to enter command mode',
      command: 'e.g., select * from sched left join thread using(utid) limit 10'
    };
    const commandMode = vnode.state.mode === 'command';
    return m(
        `.omnibox${commandMode ? '.command-mode' : ''}`,
        m(`input[type=text][placeholder=${placeholder[vnode.state.mode]}]`));
  },
};

export const Topbar: m.Component<{}, {}> = {
  view() {
    return m('.topbar', m(Omnibox));
  },
};