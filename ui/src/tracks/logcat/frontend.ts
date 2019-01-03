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

import {assertExists, assertTrue} from '../../base/logging';
import {Actions} from '../../common/actions';
import {QueryResponse} from '../../common/queries';
import {TrackState} from '../../common/state';
import {TimeSpan, timeToString} from '../../common/time';
import {checkerboardExcept} from '../../frontend/checkerboard';
import {globals} from '../../frontend/globals';
import {Panel} from '../../frontend/panel';
import {Track} from '../../frontend/track';
import {trackRegistry} from '../../frontend/track_registry';

import {Config, Data, LOGCAT_TRACK_KIND} from './common';

interface LevelCfg {
  color: string;
  prios: number[];
}

const LEVELS: LevelCfg[] = [
  {color: 'hsl(122, 39%, 49%)', prios: [0, 1, 2, 3]},  // Up to DEBUG: Green.
  {color: 'hsl(0, 0%, 70%)', prios: [4]},              // 4 (INFO) -> Gray.
  {color: 'hsl(45, 100%, 51%)', prios: [5]},           // 5 (WARN) -> Amber.
  {color: 'hsl(4, 90%, 58%)', prios: [6]},             // 6 (ERROR) -> Red.
  {color: 'hsl(291, 64%, 42%)', prios: [7]},           // 7 (FATAL) -> Purple
];

const MARGIN_TOP = 2;
const RECT_HEIGHT = 35;
const EVT_PX = 2;  // Width of an event tick in pixels.

function getCurResolution() {
  // Truncate the resolution to the closest power of 10.
  const resolution =
      globals.frontendLocalState.timeScale.deltaPxToDuration(EVT_PX);
  return Math.pow(10, Math.floor(Math.log10(resolution)));
}

class LogcatTrack extends Track<Config, Data> {
  static readonly kind = LOGCAT_TRACK_KIND;
  static create(trackState: TrackState): LogcatTrack {
    return new LogcatTrack(trackState);
  }

  private reqPending = false;

  constructor(trackState: TrackState) {
    super(trackState);
  }

  reqDataDeferred() {
    const {visibleWindowTime} = globals.frontendLocalState;
    const reqStart = visibleWindowTime.start - visibleWindowTime.duration;
    const reqEnd = visibleWindowTime.end + visibleWindowTime.duration;
    const reqRes = getCurResolution();
    this.reqPending = false;
    globals.dispatch(Actions.reqTrackData({
      trackId: this.trackState.id,
      start: reqStart,
      end: reqEnd,
      resolution: reqRes
    }));
  }

  renderCanvas(ctx: CanvasRenderingContext2D): void {
    const {timeScale, visibleWindowTime} = globals.frontendLocalState;

    const data = this.data();
    const inRange = data !== undefined &&
        (visibleWindowTime.start >= data.start &&
         visibleWindowTime.end <= data.end);
    if (!inRange || data === undefined ||
        data.resolution !== getCurResolution()) {
      if (!this.reqPending) {
        this.reqPending = true;
        setTimeout(() => this.reqDataDeferred(), 50);
      }
    }
    if (data === undefined) return;  // Can't possibly draw anything.

    const dataStartPx = timeScale.timeToPx(data.start);
    const dataEndPx = timeScale.timeToPx(data.end);
    const visibleStartPx = timeScale.timeToPx(visibleWindowTime.start);
    const visibleEndPx = timeScale.timeToPx(visibleWindowTime.end);

    checkerboardExcept(
        ctx, visibleStartPx, visibleEndPx, dataStartPx, dataEndPx);

    const quantWidth =
        Math.max(EVT_PX, timeScale.deltaTimeToPx(data.resolution));
    const BLOCK_H = RECT_HEIGHT / LEVELS.length;
    for (let i = 0; i < data.timestamps.length; i++) {
      for (let lev = 0; lev < LEVELS.length; lev++) {
        let hasEventsForCurColor = false;
        for (const prio of LEVELS[lev].prios) {
          if (data.priorities[i] & (1 << prio)) hasEventsForCurColor = true;
        }
        if (!hasEventsForCurColor) continue;
        ctx.fillStyle = LEVELS[lev].color;
        const px = Math.floor(timeScale.timeToPx(data.timestamps[i]));
        ctx.fillRect(px, MARGIN_TOP + BLOCK_H * lev, quantWidth, BLOCK_H);
      }  // for(lev)
    }    // for (timestamps)
  }
}

trackRegistry.register(LogcatTrack);

interface LogcatPanelAttrs {
  // title: string;
}

const QUERY_ID = 'logcat_table';
const PRIO_TO_LETTER = ['-', '-', 'V', 'D', 'I', 'W', 'E', 'F'];

interface CachedLogcatEntry {
  ts: number;
  prio: number;
  tag: string;
  msg: string;
}

export class LogcatPanel extends Panel<LogcatPanelAttrs> {
  private state: 'idle'|'updateBounds'|'fetchRows' = 'idle';
  private req = new TimeSpan(0, 0);
  private reqOffset = 0;
  private cache = new Map<number, CachedLogcatEntry>();
  private staleCache = new Map<number, CachedLogcatEntry>();
  private totRows = 0;
  private tbody?: HTMLElement;

  private vizRowStart = 0;
  private vizRowEnd = 0;

  renderCanvas() {
    // TODO here maybe check viz time and schedule redraw?.
  }

  maybeUpdate() {
    const vizTime = globals.frontendLocalState.visibleWindowTime;
    const vizStartNs = Math.floor(vizTime.start * 1e9);
    const vizEndNs = Math.ceil(vizTime.end * 1e9);
    const vizSqlBounds = `ts >= ${vizStartNs} and ts <= ${vizEndNs}`;

    const queryResp = globals.queryResults.get(QUERY_ID) as QueryResponse;

    switch (this.state) {
      case 'idle':
        // First of all check if the visible time window has changed. If that's
        // the case fetch the new event count for the new time bounds.
        if (!vizTime.equals(this.req)) {
          console.log('Logcat time window change, brand new request');  // DNS.
          this.state = 'updateBounds';
          this.req = vizTime.clone();
          globals.dispatch(Actions.executeQuery({
            engineId: '0',
            queryId: QUERY_ID,
            query: `select count(*) as num_rows from logcat
                    where ${vizSqlBounds}`
          }));
          return;
        }

        // Then check if all the visible rows are cached, if not fetch them.
        let needsData = false;
        for (let row = this.vizRowStart; row < this.vizRowEnd; row++) {
          if (!this.cache.has(row)) {
            needsData = true;
            break;
          }
        }
        if (!needsData) return;
        this.state = 'fetchRows';
        this.reqOffset = this.vizRowStart;
        console.log(
            `Requesting rows ${this.vizRowStart}-${this.vizRowEnd}`);  // DNS.
        globals.dispatch(Actions.executeQuery({
          engineId: '0',
          queryId: QUERY_ID,
          query: `select ts, prio, tag, msg from logcat
                      where ${vizSqlBounds}
                      limit ${this.vizRowStart},
                            ${this.vizRowEnd - this.vizRowStart + 1}`
        }));
        break;

      case 'updateBounds':
        if (queryResp !== undefined) {
          this.state = 'idle';
          console.log('tot rows reply');  // DNS.
          assertTrue(queryResp.totalRowCount === 1);
          this.totRows = +queryResp.rows[0]['num_rows'];
          globals.queryResults.delete(QUERY_ID);
          if (this.cache.size > 0) this.staleCache = this.cache;
          this.cache = new Map<number, CachedLogcatEntry>();
        }
        break;

      case 'fetchRows':
        if (queryResp !== undefined) {
          this.state = 'idle';
          console.log('full rows reply');  // DNS.
          let rowNum = this.reqOffset;
          for (const row of queryResp.rows) {
            this.cache.set(rowNum++, {
              ts: +row['ts'],
              prio: +row['prio'],
              tag: row['tag'] as string,
              msg: row['msg'] as string
            });
          }
          this.staleCache.clear();
          globals.queryResults.delete(QUERY_ID);
        }
        break;

      default:
        break;
    }
  }

  recomputeVisibleRowsAndUpdate() {
    const tbody = assertExists(this.tbody);
    const firstRow = tbody.querySelector('tr');
    const prevStart = this.vizRowStart;
    const prevEnd = this.vizRowEnd;
    if (firstRow === null) {
      this.vizRowStart = 0;
      this.vizRowEnd = 0;
    } else {
      const rowH = firstRow.scrollHeight;
      this.vizRowStart = Math.floor(tbody.scrollTop / rowH);
      this.vizRowEnd = Math.ceil((tbody.scrollTop + tbody.clientHeight) / rowH);
    }
    this.maybeUpdate();
    if (this.vizRowStart !== prevStart || this.vizRowStart !== prevEnd) {
      globals.rafScheduler.scheduleFullRedraw();
    }
  }

  onScroll() {
    if (this.tbody === undefined) return;
    this.recomputeVisibleRowsAndUpdate();
  }

  onupdate({dom}: m.CVnodeDOM) {
    this.tbody = this.tbody || assertExists(dom.querySelector('tbody'));
    this.recomputeVisibleRowsAndUpdate();
  }


  view(_: m.CVnode<LogcatPanelAttrs>) {
    this.maybeUpdate();
    const rows: m.Children = [];
    for (let rowNum = 0; rowNum < (this.totRows || 0); rowNum++) {
      let row = this.cache.get(rowNum);
      let stale = false;
      if (row === undefined) {
        row = this.staleCache.get(rowNum);
        stale = true;
      }
      if (row !== undefined) {
        rows.push(
            m(`tr${stale ? '.stale' : ''}`,
              m('td', timeToString(row.ts / 1e9)),
              m('td', PRIO_TO_LETTER[row.prio] || '?'),
              m('td', row.tag),
              m('td', row.msg)));
      } else {
        rows.push(m('tr', m('td[colspan=4]', '... loading ...')));
      }
    }
    return m(
        'div',
        m('header',
          `Logcat events. Rows [${
                                  this.vizRowStart
                                }, ${this.vizRowEnd}] / ${this.totRows || 0}`),
        m('table.logcat',
          m('tbody', {onscroll: this.onScroll.bind(this)}, rows)));
  }
}
