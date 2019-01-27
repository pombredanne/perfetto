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
import {MeminfoCounters, VmstatCounters} from '../common/protos';
import {RecordConfig, RecordMode} from '../common/state';

import {globals} from './globals';
import {createPage} from './pages';

const BUF_SIZES_MB = [4, 8, 16, 32, 64, 128, 256, 512];
const DURATIONS_MS = [
  5000,
  10000,
  15000,
  30000,
  60000,
  60000 * 5,
  60000 * 30,
  3600000,
  3600000 * 6,
  3600000 * 12
];
const FILE_SIZES_MB = [5, 25, 50, 100, 500, 1000, 1000 * 5, 1000 * 10];
const FTRACE_BUF_MB = [1, 2, 4, 8, 16, 32, 64, 128];
const FTRACE_FLUSH_MS = [100, 250, 500, 1000, 2500, 5000];
const POLL_RATE_MS = [250, 500, 1000, 2500, 5000, 30000, 60000];

declare type Setter<T> = (draft: DraftObject<RecordConfig>, val: T) => void;
declare type Getter<T> = (cfg: RecordConfig) => T;

interface SliderConfig {
  title: string;
  icon?: string;
  cssClass?: string;
  isTime?: boolean;
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

interface DropdownConfig {
  title: string;
  cssClass?: string;
  options: Map<string, string>;
  get: Getter<string[]>;
  set: Setter<string[]>;
}

function Dropdown(cfg: DropdownConfig) {
  const resetScroll = function(this: HTMLElement) {
    // Chrome seems to override the scroll offset on creation without this, even
    // though we call it after having marked the options as selected.
    setTimeout(() => {
      // Don't reset the scroll position if the element is still focused.
      if (this !== document.activeElement) this.scrollTop = 0;
    }, 0);
  };
  const options: m.Children = [];
  const selItems = cfg.get(globals.state.recordConfig);
  let numSelected = 0;
  for (const [key, label] of cfg.options) {
    const opts = {value: key, selected: false};
    if (selItems.indexOf(key) >= 0) {
      opts.selected = true;
      numSelected++;
    }
    options.push(m('option', opts, label));
  }
  const onChange = function(this: HTMLSelectElement) {
    const selKeys: string[] = [];
    for (let i = 0; i < this.selectedOptions.length; i++) {
      const opt = this.selectedOptions.item(i)!;
      selKeys.push(opt.value);
    }
    const traceCfg = produce(globals.state.recordConfig, draft => {
      cfg.set(draft, selKeys);
    });
    globals.dispatch(Actions.setConfig({config: traceCfg}));
  };
  const label = `${cfg.title} ${numSelected ? `(${numSelected})` : ''}`;
  return m(
      `select.checkboxes${cfg.cssClass || ''}[multiple=multiple]`,
      {
        onblur: resetScroll,
        onmouseleave: resetScroll,
        oninput: onChange,
        oncreate: (vnode) => {
          resetScroll.bind(vnode.dom as HTMLSelectElement)();
        }
      },
      m('optgroup', {label}, options));
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
      m('div', m('div', cfg.descr), m('.settings', children)));
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
  let spinnerCfg = {};
  if (cfg.isTime === true) {
    spinnerCfg = {
      type: 'time',
      valueAsNumber: `${val}`,
      oninput: m.withAttr('valueAsNumber', onValueChange)
    };
  } else {
    spinnerCfg = {
      type: 'number',
      value: val,
      oninput: m.withAttr('value', onValueChange)
    };
  }
  return m(
      '.slider' + (cfg.cssClass || ''),
      m('header', cfg.title),
      cfg.icon !== undefined ? m('i.material-icons', cfg.icon) : [],
      m(`input[id="${id}"][type=range][min=0][max=${maxIdx}][value=${idx}]`,
        {oninput: m.withAttr('value', onSliderChange)}),
      m(`input.spinner[min=1][for=${id}]`, spinnerCfg),
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
            predefinedValues: DURATIONS_MS,
            isTime: true,
            unit: 'h:m:s',
            set: (cfg, val) => {
              cfg.durationMs = val;
            },
            get: (cfg) => cfg.durationMs
          }),


          Slider({
            title: 'Max file size',
            icon: 'save',
            cssClass: cfg.mode !== 'LONG_TRACE' ? '.hide' : '',
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
            cssClass: cfg.mode !== 'LONG_TRACE' ? '.hide' : '',
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
                  'Polls charge counters and instantaneous power draw from ' +
                  'the battery power management IC.',
              setEnabled: (cfg, val) => {
                cfg.batteryDrain = val;
              },
              isEnabled: (cfg) => cfg.batteryDrain
            },
            Slider({
              title: 'Poll rate',
              cssClass: '.thin',
              predefinedValues: POLL_RATE_MS,
              unit: 'ms',
              set: (cfg, val) => {
                cfg.batteryPeriodMs = val;
              },
              get: (cfg) => cfg.batteryPeriodMs
            })),
        Probe({
          title: 'CPU frequency and idle states',
          img: 'cpu_freq.png',
          descr: 'Records cpu frequency and idle state changes via ftrace',
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

function CpuSettings() {
  return m(
      'div',
      m('.container',
        Probe(
            {
              title: 'Coarse CPU usage counter',
              img: 'rec_cpu_coarse.png',
              descr: `Lightweight polling of CPU usage counters via /proc/stat.
                      Allows to monitor CPU usage over fixed periods of time.`,
              setEnabled: (cfg, val) => {
                cfg.cpuCoarse = val;
              },
              isEnabled: (cfg) => cfg.cpuCoarse
            },
            Slider({
              title: 'Poll rate',
              cssClass: '.thin',
              predefinedValues: POLL_RATE_MS,
              unit: 'ms',
              set: (cfg, val) => {
                cfg.statPeriodMs = val;
              },
              get: (cfg) => cfg.statPeriodMs
            })),
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
          title: 'Scheduling chains / latency analysis',
          img: 'rec_cpu_wakeup.png',
          descr: `Tracks causality of scheduling transitions. When a task
                  X transitions from blocked -> runnable, keeps track of the
                  task Y that X's transition (e.g. posting a semaphore).`,
          setEnabled: (cfg, val) => {
            cfg.cpuLatency = val;
          },
          isEnabled: (cfg) => cfg.cpuLatency
        }), ));
}

function MemorySettings() {
  const meminfoOpts = new Map<string, string>();
  for (const x in MeminfoCounters) {
    if (typeof MeminfoCounters[x] === 'number' &&
        !`${x}`.endsWith('_UNSPECIFIED')) {
      meminfoOpts.set(x, x.replace('MEMINFO_', '').toLowerCase());
    }
  }
  const vmstatOpts = new Map<string, string>();
  for (const x in VmstatCounters) {
    if (typeof VmstatCounters[x] === 'number' &&
        !`${x}`.endsWith('_UNSPECIFIED')) {
      vmstatOpts.set(x, x.replace('VMSTAT_', '').toLowerCase());
    }
  }
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
            Slider({
              title: 'Poll rate',
              cssClass: '.thin',
              predefinedValues: POLL_RATE_MS,
              unit: 'ms',
              set: (cfg, val) => {
                cfg.meminfoPeriodMs = val;
              },
              get: (cfg) => cfg.meminfoPeriodMs
            }),
            Dropdown({
              title: 'Select counters',
              options: meminfoOpts,
              set: (cfg, val) => {
                cfg.meminfoCounters = val;
              },
              get: (cfg) => cfg.meminfoCounters
            })),
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
            Slider({
              title: 'Poll rate',
              cssClass: '.thin',
              predefinedValues: POLL_RATE_MS,
              unit: 'ms',
              set: (cfg, val) => {
                cfg.vmstatPeriodMs = val;
              },
              get: (cfg) => cfg.vmstatPeriodMs
            }),
            Dropdown({
              title: 'Select counters',
              options: vmstatOpts,
              set: (cfg, val) => {
                cfg.vmstatCounters = val;
              },
              get: (cfg) => cfg.vmstatCounters
            }),  //
            ),
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
              img: 'procstats.png',
              descr: 'TODO poll /proc/*/stat.descr',
              setEnabled: (cfg, val) => {
                cfg.memProcStat = val;
              },
              isEnabled: (cfg) => cfg.memProcStat
            },
            Slider({
              title: 'Poll rate',
              cssClass: '.thin',
              predefinedValues: POLL_RATE_MS,
              unit: 'ms',
              set: (cfg, val) => {
                cfg.procStatusPeriodMs = val;
              },
              get: (cfg) => cfg.procStatusPeriodMs
            }), )));
}


function AdvancedSettings() {
  const ATRACE_CATEGORIES = new Map<string, string>();
  ATRACE_CATEGORIES.set('gfx', 'Graphics');
  ATRACE_CATEGORIES.set('input', 'Input');
  ATRACE_CATEGORIES.set('view', 'View System');
  ATRACE_CATEGORIES.set('webview', 'WebView');
  ATRACE_CATEGORIES.set('wm', 'Window Manager');
  ATRACE_CATEGORIES.set('am', 'Activity Manager');
  ATRACE_CATEGORIES.set('sm', 'Sync Manager');
  ATRACE_CATEGORIES.set('audio', 'Audio');
  ATRACE_CATEGORIES.set('video', 'Video');
  ATRACE_CATEGORIES.set('camera', 'Camera');
  ATRACE_CATEGORIES.set('hal', 'Hardware Modules');
  ATRACE_CATEGORIES.set('res', 'Resource Loading');
  ATRACE_CATEGORIES.set('dalvik', 'ART & Dalvik');
  ATRACE_CATEGORIES.set('rs', 'RenderScript');
  ATRACE_CATEGORIES.set('bionic', 'Bionic C library');
  ATRACE_CATEGORIES.set('gfx', 'Graphics');
  ATRACE_CATEGORIES.set('power', 'Power Management');
  ATRACE_CATEGORIES.set('pm', 'Package Manager');
  ATRACE_CATEGORIES.set('ss', 'System Server');
  ATRACE_CATEGORIES.set('database', 'Database');
  ATRACE_CATEGORIES.set('network', 'Network');
  ATRACE_CATEGORIES.set('adb', 'ADB');
  ATRACE_CATEGORIES.set('vibrartor', 'Vibrator');
  ATRACE_CATEGORIES.set('aidl', 'AIDL calls');
  ATRACE_CATEGORIES.set('nnapi', 'Neural Network API');
  ATRACE_CATEGORIES.set('rro', 'Resource Overlay');

  return m(
      'div',
      m('.container',
        Probe(
            {
              title: 'Atrace userspace annotations',
              img: 'rec_atrace.png',
              descr:
                  `Enables C++ / Java codebase annotations via TRACE / os.Trace() `,
              setEnabled: (cfg, val) => {
                cfg.atrace = val;
              },
              isEnabled: (cfg) => cfg.atrace
            },
            Dropdown({
              title: 'Categories',
              cssClass: '.multicolumn.atrace_categories',
              options: ATRACE_CATEGORIES,
              set: (cfg, val) => {
                cfg.atraceCategories = val;
              },
              get: (cfg) => cfg.atraceCategories
            }), ),
        Probe({
          title: 'Extra kernel events',
          img: 'rec_ftrace.png',
          descr: `Enables extra events from the ftrace kernel tracer.`,
          setEnabled: (cfg, val) => {
            cfg.ftrace = val;
          },
          isEnabled: (cfg) => cfg.ftrace
        }, )));
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
      case 'advanced':
        page = AdvancedSettings();
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
                  m('.title', 'Recording options'),
                  m('.sub', 'Buffer mode, size and duration'))),
              m('a[href="#!/record/advanced"]',
                m(`li${routePage === 'advanced' ? '.active' : ''}`,
                  m('i.material-icons', 'settings'),
                  m('.title', 'Advanced'),
                  m('.sub', 'Advanced ftrace & atrace settings'))), ),
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
              //     m('a[href="#!/record/io"]',
              //   m(`li${routePage === 'io' ? '.active' : ''}`,
              //     m('i.material-icons', 'sd_storage'),
              //     m('.title', 'Disk I/O'),
              //     m('.sub', 'TODO'))),
              // m('a[href="#!/record/peripherals"]',
              //   m(`li${routePage === 'peripherals' ? '.active' : ''}`,
              //     m('i.material-icons', 'developer_board'),
              //     m('.title', 'Peripherals'),
              //     m('.sub', 'I2C, radio and other stuff'))),
              ),
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
