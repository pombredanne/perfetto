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

interface OmniboxAttrs {
  selResult: number;
  numResults: number;
}

function clearOmniboxResults() {
  globals.queryResults.delete(QUERY_ID);
  globals.dispatch(deleteQuery(QUERY_ID));
}

function onKeyUp(state: OmniboxAttrs, e: Event) {
  e.stopPropagation();
  const key = (e as KeyboardEvent).key;
  const txt = e.target as HTMLInputElement;
  if (key === 'ArrowUp' || key === 'ArrowDown') {
    state.selResult += (key === 'ArrowUp') ? -1 : 1;
    state.selResult = Math.max(state.selResult, 0);
    state.selResult = Math.min(state.selResult, state.numResults - 1);
    m.redraw();
    e.preventDefault();
    return;
  }
  if (txt.value.length <= 0 || key === 'Escape') {
    clearOmniboxResults();
    return;
  }
  const name = txt.value.replace(/'/g, '\\\'').replace(/[*]/g, '%');
  const query = `select * from process where name like '%${name}%' limit 10`;
  globals.dispatch(executeQuery('0', QUERY_ID, query));
}

const Omnibox: m.Component<{}, OmniboxAttrs> = {
  oncreate(vdom) {
    const txt = vdom.dom.querySelector('input') as HTMLInputElement;
    vdom.state.selResult = 0;
    vdom.state.numResults = 0;
    txt.addEventListener('blur', clearOmniboxResults);
    txt.addEventListener('keyup', onKeyUp.bind(undefined, vdom.state));

    // Avoid that the global 'a', 'd', 'w', 's' handler sees these keystrokes.
    // TODO: this seems a bug in the pan_and_zoom_handler.ts.
    txt.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
        e.preventDefault();
      }
      e.stopPropagation();
    });
  },
  view(vdom) {
    const results = [];
    const resp = globals.queryResults.get(QUERY_ID) as QueryResponse;
    if (resp !== undefined) {
      vdom.state.numResults = resp.rows ? resp.rows.length : 0;
      for (let i = 0; i < resp.rows.length; i++) {
        const row = resp.rows[i];
        const clazz = (i === vdom.state.selResult) ? '.selected' : '';
        results.push(m(`div${clazz}`, row.name));
      }
    }

    const placeholder = 'Type to search';
    return m(
        '.omnibox',
        m(`input[type=text][placeholder=${placeholder}]`),
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