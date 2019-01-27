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

import {DraftObject, produce} from 'immer';
import * as m from 'mithril';

import {Actions} from '../common/actions';
import {RecordConfig, RecordMode} from '../common/state';

import {globals} from './globals';
import {createPage} from './pages';


const BUF_SIZES_MB = [4, 8, 16, 32, 64, 128, 256, 512];
const DURATIONS_S = [10, 30, 60, 60 * 5, 60 * 30, 3600, 3600 * 6, 3600 * 12];
const FILE_SIZES_MB = [5, 100, 500, 1000, 1000 * 5, 1000 * 10];
const FTRACE_BUF_MB = [1, 2, 4, 8, 16, 32, 64, 128];
const FTRACE_FLUSH_MS = [100, 250, 500, 1000, 2500, 5000];

declare type Setter<T> = (draft: DraftObject<RecordConfig>, val: T) => void;
declare type Getter<T> = (cfg: RecordConfig) => T;

interface SliderConfig {
  title: string;
  icon: string;
  unit: string;
  predefinedValues: number[];
  get: Getter<number>;
  set: Setter<number>;
}

interface ProbeConfig {
  title: string;
  img: string;
  descr: string;
  isEnabled: Getter<boolean>;
  setEnabled: Setter<boolean>;
}

function Probe(cfg: ProbeConfig, ...children: m.Children[]) {
  const onToggle = (enabled: boolean) => {
    const traceCfg = produce(globals.state.recordConfig, draft => {
      cfg.setEnabled(draft, enabled);
    });
    globals.dispatch(Actions.setConfig({config: traceCfg}));
  };

  const enabled = cfg.isEnabled(globals.state.recordConfig);

  return m(
      `.probe${enabled ? '.enabled' : ''}`,
      m(`img[src=assets/${cfg.img}]`, {
        onclick: () => {
          onToggle(!enabled);
        }
      }),
      m('label',
        m(`input[type=checkbox]`,
          {checked: enabled, oninput: m.withAttr('checked', onToggle)}),
        m('span', cfg.title)),
      m('div', m('div', cfg.descr), children));
}

function Slider(cfg: SliderConfig) {
  const id = cfg.title.replace(/[^a-z0-9]/gmi, '_').toLowerCase();

  const onValueChange = (newVal: number) => {
    const traceCfg = produce(globals.state.recordConfig, draft => {
      cfg.set(draft, newVal);
    });
    globals.dispatch(Actions.setConfig({config: traceCfg}));
  };

  const onSliderChange = (newIdx: number) => {
    onValueChange(cfg.predefinedValues[newIdx]);
  };

  const maxIdx = cfg.predefinedValues.length - 1;
  const val = cfg.get(globals.state.recordConfig);

  // Find the index of the closest value in the slider.
  let idx = 0;
  for (let i = 0; i < cfg.predefinedValues.length; i++) {
    idx = i;
    if (cfg.predefinedValues[i] >= val) break;
  }

  return m(
      '.slider',
      m('header', cfg.title),
      m('i.material-icons', cfg.icon),
      m(`input[id="${id}"][type=range][min=0][max=${maxIdx}][value=${idx}]`,
        {oninput: m.withAttr('value', onSliderChange)}),
      m(`input[type=number][min=1][for=${id}][value="${val}"]`,
        {oninput: m.withAttr('value', onValueChange)}),
      m('.unit', cfg.unit));
}

function RecSettings() {
  const cfg = globals.state.recordConfig;

  const recButton = (mode: RecordMode, title: string, img: string) => {
    const checkboxArgs = {
      checked: cfg.mode === mode,
      onchange: m.withAttr(
          'checked',
          (checked: boolean) => {
            if (!checked) return;
            const traceCfg = produce(globals.state.recordConfig, draft => {
              draft.mode = mode;
            });
            globals.dispatch(Actions.setConfig({config: traceCfg}));
          })
    };
    return m(
        `label${cfg.mode === mode ? '.selected' : ''}`,
        m(`input[type=radio][name=rec_mode]`, checkboxArgs),
        m(`img[src=assets/${img}]`),
        m('span', title));
  };

  return m(
      'div',
      m('.container',
        m('header', 'Recording mode'),
        m('.rec_sliders',
          m('.g3',
            recButton('STOP_WHEN_FULL', 'Stop when full', 'rec_one_shot.png'),
            recButton('RING_BUFFER', 'Ring buffer', 'rec_ring_buf.png'),
            recButton('LONG_TRACE', 'Long trace', 'rec_long_trace.png'), ),

          Slider({
            title: 'In-memory buffer size',
            icon: '360',
            predefinedValues: BUF_SIZES_MB,
            unit: 'MB',
            set: (cfg, val) => {
              cfg.bufferSizeMb = val;
            },
            get: (cfg) => cfg.bufferSizeMb
          }),

          Slider({
            title: 'Max duration',
            icon: 'timer',
            predefinedValues: DURATIONS_S,
            unit: 's',
            set: (cfg, val) => {
              cfg.durationSeconds = val;
            },
            get: (cfg) => cfg.durationSeconds
          }),

          Slider({
            title: 'Max file size',
            icon: 'save',
            predefinedValues: FILE_SIZES_MB,
            unit: 'MB',
            set: (cfg, val) => {
              cfg.maxFileSizeMb = val;
            },
            get: (cfg) => cfg.maxFileSizeMb
          })),

        m('.wizard',

          Slider({
            title: 'Ftrace kernel buffer size',
            icon: 'donut_large',
            predefinedValues: FTRACE_BUF_MB,
            unit: 'MB',
            set: (cfg, val) => {
              cfg.ftraceBufferSizeKb = val;
            },
            get: (cfg) => cfg.ftraceBufferSizeKb || 0
          }),

          Slider({
            title: 'Flush on disk every',
            icon: 'av_timer',
            predefinedValues: FTRACE_FLUSH_MS,
            unit: 'ms',
            set: (cfg, val) => {
              cfg.fileWritePeriodMs = val;
            },
            get: (cfg) => cfg.fileWritePeriodMs || 0
          }))));
}

function PowerSettings() {
  return m(
      'div',
      m('.container',
        Probe(
            {
              title: 'Battery drain',
              img: 'battery_counters.png',
              descr:
                  'Tracks charge counters and instantaneous power draw from ' +
                  'the battery power management IC.',
              setEnabled: (cfg, val) => {
                cfg.batteryDrain = val;
              },
              isEnabled: (cfg) => cfg.batteryDrain
            },
            m('.settings', SliderThin('Poll rate'))),
        Probe({
          title: 'CPU frequency and idle states',
          img: 'cpu_freq.png',
          descr: 'TODO description',
          setEnabled: (cfg, val) => {
            cfg.cpuFreq = val;
          },
          isEnabled: (cfg) => cfg.cpuFreq
        }),
        Probe({
          title: 'Board voltages & frequencies',
          img: 'board_voltage.png',
          descr: 'Tracks voltage and frequency changes from board sensors',
          setEnabled: (cfg, val) => {
            cfg.boardSensors = val;
          },
          isEnabled: (cfg) => cfg.boardSensors
        }), ));
}

function SliderThin(title: string) {
  return m(
      '.slider .thin',
      m('header', title),
      m(`input[id="rec_period"][type=range][min=0][max=100][value=10]`, {}),
      m('label[for="rec_period"]', `0 s`));
}


function CpuSettings() {
  return m(
      'div',
      m('.container',
        Probe(
            {
              title: 'Coarse CPU usage counter',
              img: 'rec_cpu_coarse.png',
              descr: 'Lightweight polling of CPU usage counters.\n' +
                  'Allows to monitor CPU usage over fixed periods of time.',
              setEnabled: (cfg, val) => {
                cfg.cpuCoarse = val;
              },
              isEnabled: (cfg) => cfg.cpuCoarse
            },
            m('.settings', SliderThin('Poll rate'))),
        Probe({
          title: 'Scheduling details',
          img: 'rec_cpu_fine.png',
          descr: 'Enables high-detailed tracking of scheduling events',
          setEnabled: (cfg, val) => {
            cfg.cpuSched = val;
          },
          isEnabled: (cfg) => cfg.cpuSched
        }),
        Probe({
          title: 'Scheduling chains & latency',
          img: 'rec_cpu_wakeup.png',
          descr: 'Enables tracking of scheduling wakeup causes',
          setEnabled: (cfg, val) => {
            cfg.cpuLatency = val;
          },
          isEnabled: (cfg) => cfg.cpuLatency
        }), ));
}

function MemorySettings() {
  return m(
      'div',
      m('.container',
        Probe(
            {
              title: 'Kernel meminfo',
              img: 'meminfo.png',
              descr: 'Polling of /proc/meminfo',
              setEnabled: (cfg, val) => {
                cfg.memMeminfo = val;
              },
              isEnabled: (cfg) => cfg.memMeminfo
            },
            m('.settings', SliderThin('Poll rate'))),

        Probe(
            {
              title: 'Virtual memory stats',
              img: 'vmstat.png',
              descr: 'Polling of /proc/vmstat. TODO ftrace counters',
              setEnabled: (cfg, val) => {
                cfg.memVmstat = val;
              },
              isEnabled: (cfg) => cfg.memVmstat
            },
            m('.settings', SliderThin('Poll rate'))),

        m('header', 'Per-process probes'),

        Probe({
          title: 'Low memory killer',
          img: 'lmk.png',
          descr: 'TODO descr',
          setEnabled: (cfg, val) => {
            cfg.memLmk = val;
          },
          isEnabled: (cfg) => cfg.memLmk
        }),

        Probe(
            {
              title: 'Per process stats',
              img: 'lmk.png',
              descr: 'TODO poll /proc/*/stat.descr',
              setEnabled: (cfg, val) => {
                cfg.memProcStat = val;
              },
              isEnabled: (cfg) => cfg.memProcStat
            },
            m('.checkboxes',
              m('label', m('input[type=checkbox]'), m('.ui', 'MEM_TOTAL')),
              m('label', m('input[type=checkbox]'), m('.ui', 'MEM_FREE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'MEM_AVAILABLE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'BUFFERS')),
              m('label', m('input[type=checkbox]'), m('.ui', 'CACHED')),
              m('label', m('input[type=checkbox]'), m('.ui', 'SWAP_CACHED')),
              m('label', m('input[type=checkbox]'), m('.ui', 'ACTIVE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'INACTIVE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'ACTIVE_ANON')),
              m('label', m('input[type=checkbox]'), m('.ui', 'INACTIVE_ANON')),
              m('label', m('input[type=checkbox]'), m('.ui', 'ACTIVE_FILE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'INACTIVE_FILE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'UNEVICTABLE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'MLOCKED')),
              m('label', m('input[type=checkbox]'), m('.ui', 'SWAP_TOTAL')),
              m('label', m('input[type=checkbox]'), m('.ui', 'SWAP_FREE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'DIRTY')),
              m('label', m('input[type=checkbox]'), m('.ui', 'WRITEBACK')),
              m('label', m('input[type=checkbox]'), m('.ui', 'ANON_PAGES')),
              m('label', m('input[type=checkbox]'), m('.ui', 'MAPPED')),
              m('label', m('input[type=checkbox]'), m('.ui', 'SHMEM')),
              m('label', m('input[type=checkbox]'), m('.ui', 'SLAB')),
              m('label',
                m('input[type=checkbox]'),
                m('.ui', 'SLAB_RECLAIMABLE')),
              m('label',
                m('input[type=checkbox]'),
                m('.ui', 'SLAB_UNRECLAIMABLE')),
              m('label', m('input[type=checkbox]'), m('.ui', 'KERNEL_STACK')),
              m('label', m('input[type=checkbox]'), m('.ui', 'PAGE_TABLES')),
              m('label', m('input[type=checkbox]'), m('.ui', 'COMMIT_LIMIT')),
              m('label', m('input[type=checkbox]'), m('.ui', 'COMMITED_AS')),
              m('label', m('input[type=checkbox]'), m('.ui', 'VMALLOC_TOTAL')),
              m('label', m('input[type=checkbox]'), m('.ui', 'VMALLOC_USED')),
              m('label', m('input[type=checkbox]'), m('.ui', 'VMALLOC_CHUNK')),
              m('label', m('input[type=checkbox]'), m('.ui', 'CMA_TOTAL')),
              m('label', m('input[type=checkbox]'), m('.ui', 'CMA_FREE')),
              //
              ))));
}

export const RecordPage = createPage({
  view() {
    let routePage = '';
    let page = undefined;
    if (globals.state.route) {
      routePage = globals.state.route.split('/').splice(-1)[0] || '';
    }
    switch (routePage) {
      case 'cpu':
        page = CpuSettings();
        break;
      case 'power':
        page = PowerSettings();
        break;
      case 'memory':
        page = MemorySettings();
        break;
      default:
        page = RecSettings();
        routePage = 'buffers';
    }

    return m(
        '.record-page',
        m('.record-container',
          m('.menu',
            m('header', 'Global settings'),
            m('ul',
              m('a[href="#!/record/buffers"]',
                m(`li${routePage === 'buffers' ? '.active' : ''}`,
                  m('i.material-icons', 'tune'),
                  m('.title', 'Recording settings'),
                  m('.sub', 'Trace duration, buffer sizes'), ), ), ),
            m('header', 'Platform probes'),
            m('ul',
              m('a[href="#!/record/cpu"]',
                m(`li${routePage === 'cpu' ? '.active' : ''}`,
                  m('i.material-icons', 'subtitles'),
                  m('.title', 'CPU'),
                  m('.sub', 'CPU usage, scheduling, wakeups'))),
              m('a[href="#!/record/power"]',
                m(`li${routePage === 'power' ? '.active' : ''}`,
                  m('i.material-icons', 'battery_charging_full'),
                  m('.title', 'Power'),
                  m('.sub', 'Battery and other energy counters'))),
              m('a[href="#!/record/memory"]',
                m(`li${routePage === 'memory' ? '.active' : ''}`,
                  m('i.material-icons', 'memory'),
                  m('.title', 'Memory'),
                  m('.sub', 'Physical mem, VM, LMK'))),
              m('a[href="#!/record/io"]',
                m(`li${routePage === 'io' ? '.active' : ''}`,
                  m('i.material-icons', 'sd_storage'),
                  m('.title', 'Disk I/O'),
                  m('.sub', 'TODO'))),
              m('a[href="#!/record/peripherals"]',
                m(`li${routePage === 'peripherals' ? '.active' : ''}`,
                  m('i.material-icons', 'developer_board'),
                  m('.title', 'Peripherals'),
                  m('.sub', 'I2C, radio and other stuff'))), ),
            m('header', 'Android'),
            m('li',
              m('i.material-icons', 'android'),
              m('.title', 'Android framework'),
              m('.sub', 'TODO'), ),
            m('li',
              m('i.material-icons', 'receipt'),
              m('.title', 'Event logs'),
              m('.sub', 'App & sys logs seen in logcat'), ), ),
          m('.content', page)));
  }
});
