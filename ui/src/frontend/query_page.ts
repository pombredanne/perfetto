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

// tslint:disable no-any

import * as m from 'mithril';

import {Engine} from '../controller/engine';
import {gEngines} from './globals';
import {createPage} from './pages';

const Log: LogEntry[] = [];

interface LogEntry {
  query: string;
  success: boolean;
  rowCount: number;
  result: any,
}
;

async function doQuery(engine: Engine, query: string) {
  const log: Partial<LogEntry> = {
    query,
    success: true,
    rowCount: 0,
  };

  try {
    const result = await engine.rawQuery({
      sqlQuery: query,
    });
    console.log(result);
    log.rowCount = +result.numRecords;
    log.result = result;
  } catch (error) {
    log.success = false;
  }
  Log.unshift(log as LogEntry);
  m.redraw();
}

function table(result: any): any {
  if (!result) return m('');

  const extract = (d: any, i: number): number | string => {
    if (d.longValues.length > 0) return d.longValues[i];
    if (d.doubleValues.length > 0) return d.doubleValues[i];
    if (d.stringValues.length > 0) return d.stringValues[i];
    return 0;
  };
  return m(
      'table',
      m('thead',
        m('tr', result.columnDescriptors.map((d: any) => m('th', d.name)))),
      m('tbody', Array.from(Array(1000).keys()).map(i => {
        return m('tr', result.columns.map((c: any) => {
          return m('td', extract(c, i));
        }));
      })), );
}

const ExampleQuery = {
  view(vnode) {
    return m(
        'a[href=#]',
        {
          onclick: (e: Event) => {
            e.preventDefault();
            vnode.attrs.chosen();
          },
        },
        vnode.children);
  }
} as m.Component<{chosen: () => void}>;

const QueryBox = {
  view() {
    return m(
        'form',
        {
          onsubmit: (e: Event) => {
            e.preventDefault();
            console.log(this.query);
            const engine = gEngines.get('0');
            if (!engine) return;
            doQuery(engine, this.query);
          },
        },
        m('input[placeholder=Query].query-input', {
          disabled: !gEngines.get('0'),
          oninput: m.withAttr('value', (q: string) => this.query = q),
          value: this.query,
        }),
        'Examples: ',
        m(ExampleQuery,
          {
            chosen: () => this.query = 'select * from sched;',
          },
          'all sched events'),
        ', ',
        m(ExampleQuery,
          {
            chosen: () => this.query = 'select * from sched where cpu = 1;',
          },
          'sched events for cpu 1'), );
  }
} as m.Component<{}, {query: string}>;

export const QueryPage = createPage({
  view() {
    return m(
        '.query-page',
        m(QueryBox),
        Log.map(
            (entry: LogEntry) =>
                m('.query-log-entry',
                  m('.query-log-entry-query', entry.query),
                  m('.query-log-entry-stats', entry.rowCount),
                  m('.query-log-entry-result', table(entry.result)), )), );
  }
});
