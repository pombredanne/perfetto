/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import TraceProcessor from "./trace_processor"

import * as x from './gen/protos';
import protos = x.perfetto.protos;

var fileElement = document.getElementById('traceFile');
var consoleElement = document.getElementById('console');
var queryElement = document.getElementById('query');
var tp : TraceProcessor;

if (!fileElement || !consoleElement || !queryElement)
    throw new Error('Cannot find DIV elements');

queryElement.addEventListener('keyup', (e) => {
  if (e.keyCode != 13 || !tp || !queryElement)
    return;
  const sql : string = (<any>queryElement).value;
  let rawQuery = protos.RawQueryArgs.create();
  rawQuery.sqlQuery = sql;
  tp.raw_query.execute(rawQuery).then(() => {});
});

fileElement.addEventListener('change', () => {
    tp = new TraceProcessor((<any> fileElement).files[0], <HTMLElement> consoleElement);
    let rawQuery = protos.RawQueryArgs.create();
    rawQuery.sqlQuery = 'select * from trace limit 1';  // kick the indexing.
    tp.raw_query.execute(rawQuery).then(() => {});
});
