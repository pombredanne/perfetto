// Copyright (C) 2019 The Android Open Source Project
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

import {assertExists} from '../base/logging';
import {Actions} from '../common/actions';
import {timeToTimecode} from '../common/time';
import {LogEntries, LogBounds} from '../common/logs';
import {globals} from './globals';
import {Panel} from './panel';

const ROW_H = 20;

const PRIO_TO_LETTER = ['-', '-', 'V', 'D', 'I', 'W', 'E', 'F'];

export class LogPanel extends Panel<{}> {
  private scrollContainer?: HTMLElement;
  private bounds?: LogBounds;
  private entries?: LogEntries;

  private vizRowStart = 0;
  private vizRowEnd = 0;

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
    if (this.vizRowStart !== prevStart || this.vizRowEnd !== prevEnd) {
      globals.dispatch(Actions.updateLogsPagination({
        offset: this.vizRowStart,
        count: this.vizRowEnd - this.vizRowStart,
      }));
      globals.rafScheduler.scheduleFullRedraw();
    }
  }

  oncreate({dom}: m.CVnodeDOM) {
    this.scrollContainer = assertExists(
        dom.querySelector('.scrolling-container') as HTMLElement);
    this.scrollContainer.addEventListener(
        'scroll', this.onScroll.bind(this), {passive: true});
    this.recomputeVisibleRowsAndUpdate();
  }

  onupdate(_: m.CVnodeDOM) {
    this.bounds = globals.trackDataStore.get('log-bounds') as LogBounds;
    this.entries = globals.trackDataStore.get('log-entries') as LogEntries;
    this.recomputeVisibleRowsAndUpdate();
  }

  onScroll() {
    if (this.scrollContainer === undefined) return;
    this.recomputeVisibleRowsAndUpdate();
  }

  onRowOver(ts: number) {
    globals.frontendLocalState.setHoveredTimestamp(ts);
  }

  onRowOut() {
    globals.frontendLocalState.setHoveredTimestamp(-1);
  }

  private totalRows(): [boolean, number] {
    if (!this.bounds) return [true, 0];
    const isStale = false;
    return [isStale, this.bounds.count];
  }

  view(_: m.CVnode<{}>) {
    const rows: m.Children = [];
    if (this.entries) {
      const offset = this.entries.offset;
      const timestamps = this.entries.timestamps;
      const priorities = this.entries.priorities;
      const tags = this.entries.tags;
      const messages = this.entries.messages;
      for (let i=0; i<this.entries.timestamps.length; i++) {
        const priorityLetter = PRIO_TO_LETTER[priorities[i]];
        const ts = timestamps[i];
        let prioClass = priorityLetter || '';
        rows.push(
            m(`.row.${prioClass}`,
              {
                style: {top: `${(offset + i) * ROW_H}px`},
                onmouseover: this.onRowOver.bind(this, ts / 1e9),
                onmouseout: this.onRowOut.bind(this),
              },
              m('.cell',
                timeToTimecode(ts / 1e9 - globals.state.traceTime.startSec)),
              m('.cell', priorityLetter || '?'),
              m('.cell', tags[i]),
              m('.cell', messages[i])));
      }
    }

    const [staleTotalRows, totalRows] = this.totalRows();
    void staleTotalRows;
    const vizRange = `[${this.vizRowStart}, ${this.vizRowEnd}]`;
    return m('.log-panel',
       m('header', `Logs rows ${vizRange} / ${totalRows}`),
        m('.scrolling-container',
          m('.rows', {style: {height: `${totalRows * ROW_H}px`}}, rows)));
  }

  renderCanvas() {}
}

