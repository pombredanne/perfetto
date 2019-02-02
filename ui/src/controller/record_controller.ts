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

import {
  AndroidPowerConfig,
  BufferConfig,
  DataSourceConfig,
  FtraceConfig,
  ProcessStatsConfig,
  SysStatsConfig,
  TraceConfig
} from '../common/protos';
import {MeminfoCounters, VmstatCounters} from '../common/protos';
import {RecordConfig} from '../common/state';

import {Controller} from './controller';
import {App} from './globals';

export function uint8ArrayToBase64(buffer: Uint8Array): string {
  return btoa(String.fromCharCode.apply(null, buffer));
}

export function genConfigProto(uiCfg: RecordConfig): Uint8Array {
  const protoCfg = new TraceConfig();
  // TODO check stuff unsupported on P.
  protoCfg.durationMs = uiCfg.durationMs;
  protoCfg.buffers.push(new BufferConfig());
  protoCfg.buffers[0].sizeKb = uiCfg.bufferSizeMb * 1024;
  if (uiCfg.mode === 'STOP_WHEN_FULL') {
    protoCfg.buffers[0].fillPolicy = BufferConfig.FillPolicy.DISCARD;
  } else {
    protoCfg.buffers[0].fillPolicy = BufferConfig.FillPolicy.RING_BUFFER;
    if (uiCfg.mode === 'LONG_TRACE') {
      protoCfg.writeIntoFile = true;
      protoCfg.fileWritePeriodMs = uiCfg.fileWritePeriodMs;
      protoCfg.maxFileSizeBytes = uiCfg.maxFileSizeMb * 1e6;
    }
  }

  const ftraceEvents = new Set<string>(uiCfg.ftrace ? uiCfg.ftraceEvents : []);
  const atraceCats = new Set<string>(uiCfg.atrace ? uiCfg.atraceCats : []);
  const atraceApps = new Set<string>(uiCfg.atrace ? uiCfg.atraceApps : []);
  let enableProcScraping = false;
  let trackProcLifetime = false;
  let trackInitialOomScore = false;

  if (uiCfg.cpuSched) {
    trackProcLifetime = true;
    enableProcScraping = true;
    ftraceEvents.add('sched/sched_switch');
    ftraceEvents.add('power/suspend_resume');
  }

  if (uiCfg.cpuLatency) {
    trackProcLifetime = true;
    enableProcScraping = true;
    ftraceEvents.add('sched/sched_wakeup');
    ftraceEvents.add('sched/sched_wakeup_new');
    ftraceEvents.add('power/suspend_resume');
  }

  if (uiCfg.cpuFreq) {
    ftraceEvents.add('power/cpu_frequency');
    ftraceEvents.add('power/suspend_resume');
  }

  if (trackProcLifetime) {
    ftraceEvents.add('sched/sched_process_exit');
    ftraceEvents.add('sched/sched_process_fork');
    ftraceEvents.add('sched/sched_process_free');
    ftraceEvents.add('task/task_rename');
  }

  if (uiCfg.batteryDrain) {
    const ds = new TraceConfig.DataSource();
    ds.config = new DataSourceConfig();
    ds.config.name = 'android.power';
    ds.config.androidPowerConfig = new AndroidPowerConfig();
    ds.config.androidPowerConfig.batteryPollMs = uiCfg.batteryDrainPollMs;
    ds.config.androidPowerConfig.batteryCounters = [
      AndroidPowerConfig.BatteryCounters.BATTERY_COUNTER_CAPACITY_PERCENT,
      AndroidPowerConfig.BatteryCounters.BATTERY_COUNTER_CHARGE,
      AndroidPowerConfig.BatteryCounters.BATTERY_COUNTER_CURRENT,
    ];
    protoCfg.dataSources.push(ds);
  }

  if (uiCfg.boardSensors) {
    ftraceEvents.add('regulator/regulator_set_voltage');
    ftraceEvents.add('regulator/regulator_set_voltage_complete');
    ftraceEvents.add('power/clock_enable');
    ftraceEvents.add('power/clock_disable');
    ftraceEvents.add('power/clock_set_rate');
    ftraceEvents.add('power/suspend_resume');
  }

  let sysStatsCfg: SysStatsConfig|undefined = undefined;

  if (uiCfg.cpuCoarse) {
    if (sysStatsCfg === undefined) sysStatsCfg = new SysStatsConfig();
    sysStatsCfg.statPeriodMs = uiCfg.cpuCoarsePollMs;
    sysStatsCfg.statCounters = [
      SysStatsConfig.StatCounters.STAT_CPU_TIMES,
      SysStatsConfig.StatCounters.STAT_FORK_COUNT,
    ];
  }

  if (uiCfg.meminfo) {
    if (sysStatsCfg === undefined) sysStatsCfg = new SysStatsConfig();
    sysStatsCfg.meminfoPeriodMs = uiCfg.meminfoPeriodMs;
    sysStatsCfg.meminfoCounters = uiCfg.meminfoCounters.map(name => {
      // tslint:disable-next-line no-any
      return MeminfoCounters[name as any as number] as any as number;
    });
  }

  if (uiCfg.vmstat) {
    if (sysStatsCfg === undefined) sysStatsCfg = new SysStatsConfig();
    sysStatsCfg.vmstatPeriodMs = uiCfg.vmstatPeriodMs;
    sysStatsCfg.vmstatCounters = uiCfg.vmstatCounters.map(name => {
      // tslint:disable-next-line no-any
      return VmstatCounters[name as any as number] as any as number;
    });
  }

  if (uiCfg.memLmk) {
    // For in-kernel LMK (roughly older devices until Go and Pixel 3).
    ftraceEvents.add('sched/lowmemorykiller/lowmemory_kill');

    // For userspace LMKd (newer devices).
    // 'lmkd' is not really required because the code in lmkd.c emits events
    // with ATRACE_TAG_ALWAYS. We need something just to ensure that the final
    // config will enable atrace userspace events.
    atraceApps.add('lmkd');

    ftraceEvents.add('oom/oom_score_adj_update');
    trackInitialOomScore = true;
  }

  // TODO here, think also to ps tree.
  if (uiCfg.procStats || trackInitialOomScore) {
    const ds = new TraceConfig.DataSource();
    ds.config = new DataSourceConfig();
    ds.config.name = 'linux.process_stats';
    ds.config.processStatsConfig = new ProcessStatsConfig();
    ds.config.processStatsConfig.procStatsPollMs = uiCfg.procStatsPeriodMs;
    if (trackInitialOomScore) {
      ds.config.processStatsConfig.scanAllProcessesOnStart = true;
    }
    protoCfg.dataSources.push(ds);
  }

  // Keep these last. The stages above can enrich them.

  if (sysStatsCfg !== undefined) {
    const ds = new TraceConfig.DataSource();
    ds.config = new DataSourceConfig();
    ds.config.name = 'linux.sys_stats';
    ds.config.sysStatsConfig = sysStatsCfg;
    protoCfg.dataSources.push(ds);
  }

  if (ftraceEvents.size > 0 || atraceCats.size > 0 || atraceApps.size > 0) {
    const ds = new TraceConfig.DataSource();
    ds.config = new DataSourceConfig();
    ds.config.name = 'linux.ftrace';
    ds.config.ftraceConfig = new FtraceConfig();
    ds.config.ftraceConfig.bufferSizeKb = uiCfg.ftraceBufferSizeKb;
    ds.config.ftraceConfig.ftraceEvents = Array.from(ftraceEvents);
    ds.config.ftraceConfig.atraceCategories = Array.from(atraceCats);
    ds.config.ftraceConfig.atraceApps = uiCfg.atraceApps;
    protoCfg.dataSources.push(ds);
  }

  // TODO ion and rss_stat.

  const buffer = TraceConfig.encode(protoCfg).finish();
  return buffer;
}

export function toPbtxt(configBuffer: Uint8Array): string {
  const msg = TraceConfig.decode(configBuffer);
  const json = msg.toJSON();
  function snakeCase(s: string): string {
    return s.replace(/[A-Z]/g, c => '_' + c.toLowerCase());
  }
  // With the ahead of time compiled protos we can't seem to tell which
  // fields are enums.
  function looksLikeEnum(value: string): boolean {
    return value.startsWith('MEMINFO_') || value.startsWith('VMSTAT_') ||
        value.startsWith('STAT_') || value.startsWith('BATTERY_COUNTER_') ||
        value === 'DISCARD' || value === 'RING_BUFFER';
  }
  function* message(msg: {}, indent: number): IterableIterator<string> {
    for (const [key, value] of Object.entries(msg)) {
      const isRepeated = Array.isArray(value);
      const isNested = typeof value === 'object' && !isRepeated;
      for (const entry of (isRepeated ? value as Array<{}>: [value])) {
        yield ' '.repeat(indent) + `${snakeCase(key)}${isNested ? '' : ':'} `;
        if (typeof entry === 'string') {
          yield looksLikeEnum(entry) ? entry : `"${entry}"`;
        } else if (typeof entry === 'number') {
          yield entry.toString();
        } else if (typeof entry === 'boolean') {
          yield entry.toString();
        } else {
          yield '{\n';
          yield* message(entry, indent + 4);
          yield ' '.repeat(indent) + '}';
        }
        yield '\n';
      }
    }
  }
  return [...message(json, 0)].join('');
}

export class RecordController extends Controller<'main'> {
  private app: App;
  private config: RecordConfig|null = null;

  constructor(args: {app: App}) {
    super('main');
    this.app = args.app;
  }

  run() {
    if (this.app.state.recordConfig === this.config) return;
    this.config = this.app.state.recordConfig;
    const configProto = genConfigProto(this.config);
    const configProtoText = toPbtxt(configProto);
    const commandline = `
      echo '${uint8ArrayToBase64(configProto)}' |
      base64 --decode |
      adb shell "perfetto -c - -o /data/misc/perfetto-traces/trace" &&
      adb pull /data/misc/perfetto-traces/trace /tmp/trace
    `;
    // TODO(hjd): This should not be TrackData after we unify the stores.
    this.app.publish('TrackData', {
      id: 'config',
      data: {
        commandline,
        pbtxt: configProtoText,
      }
    });
  }
}
