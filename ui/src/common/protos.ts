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

import {assertTrue} from '../base/logging';
import * as protos from '../gen/protos';

// Aliases protos to avoid the super nested namespaces.
// See https://www.typescriptlang.org/docs/handbook/namespaces.html#aliases
import IAndroidPowerConfig = protos.perfetto.protos.IAndroidPowerConfig;
import IProcessStatsConfig = protos.perfetto.protos.IProcessStatsConfig;
import IRawQueryArgs = protos.perfetto.protos.IRawQueryArgs;
import ISysStatsConfig = protos.perfetto.protos.ISysStatsConfig;
import ITraceConfig = protos.perfetto.protos.ITraceConfig;
import BatteryCounters =
    protos.perfetto.protos.AndroidPowerConfig.BatteryCounters;
import MeminfoCounters = protos.perfetto.protos.MeminfoCounters;
import RawQueryArgs = protos.perfetto.protos.RawQueryArgs;
import RawQueryResult = protos.perfetto.protos.RawQueryResult;
import StatCounters = protos.perfetto.protos.SysStatsConfig.StatCounters;
import TraceConfig = protos.perfetto.protos.TraceConfig;
import TraceProcessor = protos.perfetto.protos.TraceProcessor;
import VmstatCounters = protos.perfetto.protos.VmstatCounters;

// TODO(hjd): Maybe these should go in their own file.
export interface Row { [key: string]: number|string; }

function getCell(result: RawQueryResult, column: number, row: number): number|
    string|null {
  const values = result.columns[column];
  if (values.isNulls![row]) return null;
  switch (result.columnDescriptors[column].type) {
    case RawQueryResult.ColumnDesc.Type.LONG:
      return +values.longValues![row];
    case RawQueryResult.ColumnDesc.Type.DOUBLE:
      return +values.doubleValues![row];
    case RawQueryResult.ColumnDesc.Type.STRING:
      return values.stringValues![row];
    default:
      throw new Error('Unhandled type!');
  }
}

export function rawQueryResultColumns(result: RawQueryResult): string[] {
  // Two columns can conflict on the same name, e.g.
  // select x.foo, y.foo from x join y. In that case store them using the
  // full table.column notation.
  const res = [] as string[];
  const uniqColNames = new Set<string>();
  const colNamesToDedupe = new Set<string>();
  for (const col of result.columnDescriptors) {
    const colName = col.name || '';
    if (uniqColNames.has(colName)) {
      colNamesToDedupe.add(colName);
    }
    uniqColNames.add(colName);
  }
  for (let i = 0; i < result.columnDescriptors.length; i++) {
    const colName = result.columnDescriptors[i].name || '';
    if (colNamesToDedupe.has(colName)) {
      res.push(`${colName}.${i + 1}`);
    } else {
      res.push(colName);
    }
  }
  return res;
}

export function* rawQueryResultIter(result: RawQueryResult) {
  const columns: Array<[string, number]> = rawQueryResultColumns(result).map(
      (name, i): [string, number] => [name, i]);
  for (let rowNum = 0; rowNum < result.numRecords; rowNum++) {
    const row: Row = {};
    for (const [name, colNum] of columns) {
      const cell = getCell(result, colNum, rowNum);
      row[name] = cell === null ? '[NULL]' : cell;
    }
    yield row;
  }
}

export const NULL: null = null;
export const NUM: number = 0;
export const NUM_NULL: number|null = 1;
export const STR: string = 'str';
export const STR_NULL: string|null = 'str_null';
export const STR_NUM: string|number = 'str_num';
export const STR_NUM_NULL: string|number|null = 'str_num_null';

/**
 * This function allows for type safe use of RawQueryResults.
 * The input is a RawQueryResult (|raw|) and a "spec".
 * A spec is an object where the keys are column names and the values
 * are constants representing the types. For example:
 * {
 *   upid: NUM,
 *   pid: NUM_NULL,
 *   processName: STR_NULL,
 * }
 * The output is a iterable of rows each row looks like the given spec:
 * {
 *   upid: 1,
 *   pid: 42,
 *   processName: null,
 * }
 * Each row has an appropriate typescript type based on the spec so there
 * is no need to use ! or cast when using the result of rawQueryToRows.
 * Note: type checking to ensure that the RawQueryResult matches the spec
 * happens at runtime, so if a query can return null and this is not reflected
 * in the spec this will still crash at runtime.
 */
export function*
    rawQueryToRows<T>(raw: RawQueryResult, spec: T): IterableIterator<T> {
  const allColumns = rawQueryResultColumns(raw);
  const columns: Array<[string, number, boolean, boolean, boolean]> = [];
  for (const [key, value] of Object.entries(spec)) {
    const index = allColumns.indexOf(key);
    assertTrue(
        index !== -1, `Expected column "${key}" (result cols ${allColumns})`);
    const nullOk = [NULL, NUM_NULL, STR_NULL, STR_NUM_NULL].includes(value);
    const numOk = [NUM, NUM_NULL, STR_NUM, STR_NUM_NULL].includes(value);
    const strOk = [STR, STR_NULL, STR_NUM, STR_NUM_NULL].includes(value);
    columns.push([key, index, nullOk, numOk, strOk]);
  }

  for (let i = 0; i < raw.numRecords; i++) {
    const row: {[_: string]: number | string | null} = {};
    for (const [name, col, nullOk, numOk, strOk] of columns) {
      const cell = getCell(raw, col, i);
      if (cell === null) assertTrue(nullOk);
      if (typeof cell === 'number') assertTrue(numOk);
      if (typeof cell === 'string') assertTrue(strOk);
      row[name] = cell;
    }
    yield row as {} as T;
  }
}

export {
  IAndroidPowerConfig,
  IProcessStatsConfig,
  IRawQueryArgs,
  ISysStatsConfig,
  ITraceConfig,
  BatteryCounters,
  MeminfoCounters,
  RawQueryArgs,
  RawQueryResult,
  StatCounters,
  TraceConfig,
  TraceProcessor,
  VmstatCounters,
};
