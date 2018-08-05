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

import {deleteQuery, executeQuery} from '../common/actions';
import {QueryResponse} from '../common/queries';
import {EngineConfig} from '../common/state';

import {globals} from './globals';

const QUERY_ID = 'quicksearch';

interface OmniboxState {
  selResult: number;
  numResults: number;
  mode: 'search'|'command';
}

function clearOmniboxResults() {
  globals.queryResults.delete(QUERY_ID);
  globals.dispatch(deleteQuery(QUERY_ID));
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
    return;
  }
  if (txt.value.length <= 0 || key === 'Escape') {
    clearOmniboxResults();
    return;
  }
  if (state.mode === 'search') {
    const name = txt.value.replace(/'/g, '\\\'').replace(/[*]/g, '%');
    const query =
        `select name from process where name like '%${name}%' limit 10`;
    globals.dispatch(executeQuery('0', QUERY_ID, query));
  }
  if (state.mode === 'command' && key == 'Enter') {
    globals.dispatch(executeQuery('0', QUERY_ID, txt.value));
  }
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
    const results = [];
    const resp = globals.queryResults.get(QUERY_ID) as QueryResponse;
    if (resp !== undefined) {
      vnode.state.numResults = resp.rows ? resp.rows.length : 0;
      for (let i = 0; i < resp.rows.length; i++) {
        const clazz = (i === vnode.state.selResult) ? '.selected' : '';
        results.push(m(`div${clazz}`, resp.rows[i][resp.columns[0]]));
      }
    }
    const placeholder = {
      search: 'Search or type : to enter command mode',
      command: 'e.g., select * from sched limit 10'
    };
    const commandMode = vnode.state.mode === 'command';
    return m(
        `.omnibox${commandMode ? '.command-mode' : ''}`,
        m(`input[type=text][placeholder=${placeholder[vnode.state.mode]}]`),
        m('.omnibox-results', results));
  },
};

export const Topbar: m.Component<{}, {}> = {
  view() {
    const progBar = [];
    const engine: EngineConfig = globals.state.engines['0'];
    if (globals.state.queries[QUERY_ID] !== undefined ||
        (engine !== undefined && !engine.ready)) {
      progBar.push(m('.progress'));
    }

    return m('.topbar', m(Omnibox), ...progBar);
  },
};