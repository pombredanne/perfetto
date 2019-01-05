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
import {TimeSpan, timeToTimecode} from '../../common/time';
import {checkerboardExcept} from '../../frontend/checkerboard';
import {globals} from '../../frontend/globals';
import {Panel} from '../../frontend/panel';
import {Track} from '../../frontend/track';
import {trackRegistry} from '../../frontend/track_registry';

import {ANDROID_LOGS_TRACK_KIND, Config, Data} from './common';

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
const ROW_H = 20;

function getCurResolution() {
  // Truncate the resolution to the closest power of 10.
  const resolution =
      globals.frontendLocalState.timeScale.deltaPxToDuration(EVT_PX);
  return Math.pow(10, Math.floor(Math.log10(resolution)));
}

class AndroidLogTrack extends Track<Config, Data> {
  static readonly kind = ANDROID_LOGS_TRACK_KIND;
  static create(trackState: TrackState): AndroidLogTrack {
    return new AndroidLogTrack(trackState);
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

trackRegistry.register(AndroidLogTrack);

interface AndroidLogPanelAttrs {
  // title: string;
}

const QUERY_ID = 'android_logs_table';
const PRIO_TO_LETTER = ['-', '-', 'V', 'D', 'I', 'W', 'E', 'F'];

interface CachedLogEntry {
  ts: number;
  prio: number;
  tag: string;
  msg: string;
  generation: number;
}

export class AndroidLogPanel extends Panel<AndroidLogPanelAttrs> {
  private state: 'idle'|'updateBounds'|'fetchRows' = 'idle';
  private req = new TimeSpan(0, 0);
  private reqOffset = 0;
  private generation = 0;
  private cache = new Array<CachedLogEntry>();
  private totRows = 0;
  private scrollContainer?: HTMLElement;

  private vizRowStart = 0;
  private vizRowEnd = 0;

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
          this.state = 'updateBounds';
          this.req = vizTime.clone();
          this.generation++;
          globals.dispatch(Actions.executeQuery({
            engineId: '0',
            queryId: QUERY_ID,
            query: `select count(*) as num_rows from android_logs
                    where ${vizSqlBounds}`
          }));
          return;
        }

        // Then check if all the visible rows are cached, if not fetch them.
        let needsData = false;
        for (let row = this.vizRowStart; row < this.vizRowEnd; row++) {
          if (!this.cache.hasOwnProperty(row) ||
              this.cache[row].generation !== this.generation) {
            needsData = true;
            break;
          }
        }
        if (!needsData) return;
        this.state = 'fetchRows';
        this.reqOffset = this.vizRowStart;
        globals.dispatch(Actions.executeQuery({
          engineId: '0',
          queryId: QUERY_ID,
          query: `select ts, prio, tag, msg from android_logs
                      where ${vizSqlBounds}
                      order by ts
                      limit ${this.vizRowStart},
                            ${this.vizRowEnd - this.vizRowStart + 1}`
        }));
        break;

      case 'updateBounds':
        if (queryResp !== undefined) {
          this.state = 'idle';
          assertTrue(queryResp.totalRowCount === 1);
          this.totRows = +queryResp.rows[0]['num_rows'];
          globals.queryResults.delete(QUERY_ID);
        }
        break;

      case 'fetchRows':
        if (queryResp !== undefined) {
          this.state = 'idle';
          let rowNum = this.reqOffset;
          for (const row of queryResp.rows) {
            this.cache[rowNum++] = {
              ts: +row['ts'],
              prio: +row['prio'],
              tag: row['tag'] as string,
              msg: row['msg'] as string,
              generation: this.generation
            };
          }
          globals.queryResults.delete(QUERY_ID);
        }
        break;

      default:
        break;
    }
  }

  recomputeVisibleRowsAndUpdate() {
    const scrollContainer = assertExists(this.scrollContainer);
    const firstRow = scrollContainer.children[0];
    const prevStart = this.vizRowStart;
    const prevEnd = this.vizRowEnd;
    if (firstRow === null) {
      this.vizRowStart = 0;
      this.vizRowEnd = 0;
    } else {
      this.vizRowStart = Math.floor(scrollContainer.scrollTop / ROW_H);
      this.vizRowEnd = Math.ceil(
          (scrollContainer.scrollTop + scrollContainer.clientHeight) / ROW_H);
    }
    this.maybeUpdate();
    if (this.vizRowStart !== prevStart || this.vizRowEnd !== prevEnd) {
        globals.rafScheduler.scheduleFullRedraw();
      }
  }

  onupdate({dom}: m.CVnodeDOM) {
    if (this.scrollContainer === undefined) {
      this.scrollContainer = assertExists(
          dom.querySelector('.scrolling_container') as HTMLElement);
      this.scrollContainer.addEventListener(
          'scroll', this.onScroll.bind(this), {passive: true});
    }
    this.recomputeVisibleRowsAndUpdate();
  }

  onScroll() {
    if (this.scrollContainer === undefined) return;
    this.recomputeVisibleRowsAndUpdate();
  }

  onRowOver(ts: number) {
    globals.frontendLocalState.highlightedTs = ts;
    globals.rafScheduler.scheduleRedraw();
  }

  onRowOut() {
    globals.frontendLocalState.highlightedTs = 0;
    globals.rafScheduler.scheduleRedraw();
  }

  view(_: m.CVnode<AndroidLogPanelAttrs>) {
    this.maybeUpdate();
    const rows: m.Children = [];
    for (const rowNum in this.cache) {
      if (!this.cache.hasOwnProperty(rowNum)) continue;
      const row = this.cache[rowNum];
      let prioClass = PRIO_TO_LETTER[row.prio] || '';
      if (row.generation !== this.generation) prioClass += '.stale';
      rows.push(
          m(`.row.${prioClass}`,
            {
              style: {top: `${+ rowNum * ROW_H}px`},
              onmouseover: this.onRowOver.bind(this, row.ts / 1e9),
              onmouseout: this.onRowOut.bind(this),
            },
            m('.cell',
              timeToTimecode(row.ts / 1e9 - globals.state.traceTime.startSec)),
            m('.cell', PRIO_TO_LETTER[row.prio] || '?'),
            m('.cell', row.tag),
            m('.cell', row.msg)));
    }

    const vizRange = `[${this.vizRowStart}, ${this.vizRowEnd}]`;
    return m(
        '.android_log_panel',
        m('header', `Android log. Rows ${vizRange} / ${this.totRows || 0}`),
        m('.scrolling_container',
          m('.rows', {style: {height: `${this.totRows * ROW_H}px`}}, rows)));
  }

  renderCanvas() {}
}
