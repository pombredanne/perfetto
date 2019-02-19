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

import {Engine} from '../common/engine';
import {TimeSpan} from '../common/time';
import {LogsPagination} from '../common/state';
import {Controller} from './controller';
import {App} from './globals';
import {LogEntries, LogBounds} from '../common/logs';

export interface LogsControllerArgs {
  engine: Engine;
  app: App;
}

async function updateLogBounds(engine: Engine, span: TimeSpan): Promise<LogBounds> {
  const vizStartNs = Math.floor(span.start * 1e9);
  const vizEndNs = Math.ceil(span.end * 1e9);
  const vizSqlBounds = `ts >= ${vizStartNs} and ts <= ${vizEndNs}`;

  const countResult = await engine.queryOneRow(
    `select count(*) as num_rows from android_logs where ${vizSqlBounds}`);
  const count = countResult[0];

  return {
    startTs: span.start,
    endTs: span.end,
    count,
  };
}

async function updateLogEntries(engine: Engine, span: TimeSpan, pagination: LogsPagination): Promise<LogEntries> {
  const vizStartNs = Math.floor(span.start * 1e9);
  const vizEndNs = Math.ceil(span.end * 1e9);
  const vizSqlBounds = `ts >= ${vizStartNs} and ts <= ${vizEndNs}`;

  const offset = pagination.offset;
  const count = Math.min(1000, Math.max(pagination.count, 100));

  const rowsResult = await engine.query(
     `select ts, prio, tag, msg from android_logs
        where ${vizSqlBounds}
        order by ts
        limit ${offset}, ${count}`);

  if (!rowsResult.numRecords) {
    return {
      offset,
      timestamps: [],
      priorities: [],
      tags: [],
      messages: [],
    };
  }

  const timestamps = rowsResult.columns[0].longValues! as number[];
  const priorities = rowsResult.columns[1].longValues! as number[];
  const tags = rowsResult.columns[2].stringValues!;
  const messages = rowsResult.columns[3].stringValues!;

  return {
    offset,
    timestamps,
    priorities,
    tags,
    messages,
  };
}

class Pagination {
  offset: number;
  count: number;

  constructor(offset: number, count: number) {
    this.offset = offset;
    this.count = count;
  }

  get end() {
    return this.offset + this.count;
  }

  contains(other: Pagination): boolean {
    return this.offset <= other.offset && other.end <= this.end;
  }
}

export class LogsController extends Controller<'main'> {
  private app: App;
  private engine: Engine;
  private span: TimeSpan;
  private pagination: Pagination;

  constructor(args: LogsControllerArgs) {
    super('main');
    this.app = args.app;
    this.engine = args.engine;
    this.span = new TimeSpan(0, 10);
    this.pagination = new Pagination(0, 1);
  }

  run() {
    const traceTime = this.app.state.frontendLocalState.visibleTraceTime;
    const newSpan = new TimeSpan(traceTime.startSec, traceTime.endSec);
    const oldSpan = this.span;

    const {offset, count} = this.app.state.logsPagination;
    const requestedPagination = new Pagination(offset, count);
    const oldPagination = this.pagination;

    const needSpanUpdate = !oldSpan.equals(newSpan);
    const needPaginationUpdate = !oldPagination.contains(requestedPagination);

    console.log('!!!!!', needSpanUpdate, needPaginationUpdate);

    if (needSpanUpdate) {
      this.span = newSpan;
      console.log('spanUpdate');

      updateLogBounds(this.engine, newSpan).then(data => {
        if (!newSpan.equals(this.span)) return;
        this.app.publish('TrackData', {
          id: 'log-bounds',
          data,
        });
      });
    }

    if (needSpanUpdate || needPaginationUpdate) {
      this.pagination = new Pagination(Math.max(0, requestedPagination.offset-50), requestedPagination.count+50);

      updateLogEntries(this.engine, newSpan, this.pagination).then(data => {
        if (!this.pagination.contains(requestedPagination)) return;
        this.app.publish('TrackData', {
          id: 'log-entries',
          data,
        });
      });
    }

    return [];
  }
}
