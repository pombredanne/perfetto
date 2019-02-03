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

import {copyToClipboard} from './clipboard';
import {globals} from './globals';
import {createPage} from './pages';
import {Router} from './router';

declare type Setter<T> = (draft: DraftObject<RecordConfig>, val: T) => void;
declare type Getter<T> = (cfg: RecordConfig) => T;

interface SliderConfig {
  title: string;
  icon?: string;
  cssClass?: string;
  isTime?: boolean;
  unit: string;
  values: number[];
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

interface TextareaConfig {
  placeholder: string;
  cssClass?: string;
  get: Getter<string>;
  set: Setter<string>;
}

interface CodeSampleAttrs {
  text: string;
  hardWhitespace?: boolean;
}

class CodeSample implements m.ClassComponent<CodeSampleAttrs> {
  view({attrs}: m.CVnode<CodeSampleAttrs>) {
    return m(
        '.example-code',
        m('button',
          {
            title: 'Copy to clipboard',
            onclick: () => copyToClipboard(attrs.text),
          },
          m('i.material-icons', 'assignment')),
        m('code',
          {
            style: {
              'white-space': attrs.hardWhitespace ? 'pre' : null,
            },
          },
          attrs.text), );
  }
}

const POLL_RATE_MS = [250, 500, 1000, 2500, 5000, 30000, 60000];

let targetOS = '';

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
      selKeys.push(this.selectedOptions.item(i).value);
    }
    const traceCfg = produce(globals.state.recordConfig, draft => {
      cfg.set(draft, selKeys);
    });
    globals.dispatch(Actions.setConfig({config: traceCfg}));
  };
  const label = `${cfg.title} ${numSelected ? `(${numSelected})` : ''}`;
  return m(
      `select.dropdown${cfg.cssClass || ''}[multiple=multiple]`,
      {
        onblur: resetScroll,
        onmouseleave: resetScroll,
        oninput: onChange,
        oncreate: (vnode) => resetScroll.bind(vnode.dom as HTMLSelectElement)(),
      },
      m('optgroup', {label}, options));
}


function Textarea(cfg: TextareaConfig) {
  const onChange = function(this: HTMLSelectElement) {
    const traceCfg = produce(globals.state.recordConfig, draft => {
      cfg.set(draft, this.value);
    });
    globals.dispatch(Actions.setConfig({config: traceCfg}));
  };
  return m(`textarea.extra_input${cfg.cssClass || ''}`, {
    onchange: onChange,
    placeholder: cfg.placeholder,
    value: cfg.get(globals.state.recordConfig)
  });
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
      m(`img[src=assets/${cfg.img}]`, {onclick: () => onToggle(!enabled)}),
      m('label',
        m(`input[type=checkbox]`,
          {checked: enabled, oninput: m.withAttr('checked', onToggle)}),
        m('span', cfg.title)),
      m('div', m('div', cfg.descr), m('.probe-config', children)));
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
    onValueChange(cfg.values[newIdx]);
  };

  const maxIdx = cfg.values.length - 1;
  const val = cfg.get(globals.state.recordConfig);

  // Find the index of the closest value in the slider.
  let idx = 0;
  for (let i = 0; i < cfg.values.length; i++) {
    idx = i;
    if (cfg.values[i] >= val) break;
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

function RecSettings(cssClass: string) {
  const S = (x: number) => x * 1000;
  const M = (x: number) => x * 1000 * 60;
  const H = (x: number) => x * 1000 * 60 * 60;

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
      `.record-section${cssClass}`,
      m('header', 'Recording mode'),
      m('.record-mode',
        recButton('STOP_WHEN_FULL', 'Stop when full', 'rec_one_shot.png'),
        recButton('RING_BUFFER', 'Ring buffer', 'rec_ring_buf.png'),
        recButton('LONG_TRACE', 'Long trace', 'rec_long_trace.png'), ),

      Slider({
        title: 'In-memory buffer size',
        icon: '360',
        values: [4, 8, 16, 32, 64, 128, 256, 512],
        unit: 'MB',
        set: (cfg, val) => cfg.bufferSizeMb = val,
        get: (cfg) => cfg.bufferSizeMb
      }),

      Slider({
        title: 'Max duration',
        icon: 'timer',
        values: [S(10), S(15), S(30), S(60), M(5), M(30), H(1), H(6), H(12)],
        isTime: true,
        unit: 'h:m:s',
        set: (cfg, val) => cfg.durationMs = val,
        get: (cfg) => cfg.durationMs
      }),
      Slider({
        title: 'Max file size',
        icon: 'save',
        cssClass: cfg.mode !== 'LONG_TRACE' ? '.hide' : '',
        values: [5, 25, 50, 100, 500, 1000, 1000 * 5, 1000 * 10],
        unit: 'MB',
        set: (cfg, val) => cfg.maxFileSizeMb = val,
        get: (cfg) => cfg.maxFileSizeMb
      }),
      Slider({
        title: 'Flush on disk every',
        cssClass: cfg.mode !== 'LONG_TRACE' ? '.hide' : '',
        icon: 'av_timer',
        values: [100, 250, 500, 1000, 2500, 5000],
        unit: 'ms',
        set: (cfg, val) => cfg.fileWritePeriodMs = val,
        get: (cfg) => cfg.fileWritePeriodMs || 0
      }));
}

function PowerSettings(cssClass: string) {
  return m(
      `.record-section${cssClass}`,
      Probe(
          {
            title: 'Battery drain',
            img: 'rec_battery_counters.png',
            descr: 'Polls charge counters and instantaneous power draw from ' +
                'the battery power management IC.',
            setEnabled: (cfg, val) => cfg.batteryDrain = val,
            isEnabled: (cfg) => cfg.batteryDrain
          },
          Slider({
            title: 'Poll rate',
            cssClass: '.thin',
            values: POLL_RATE_MS,
            unit: 'ms',
            set: (cfg, val) => cfg.batteryDrainPollMs = val,
            get: (cfg) => cfg.batteryDrainPollMs
          })),
      Probe({
        title: 'CPU frequency and idle states',
        img: 'rec_cpu_freq.png',
        descr: 'Records cpu frequency and idle state changes via ftrace',
        setEnabled: (cfg, val) => cfg.cpuFreq = val,
        isEnabled: (cfg) => cfg.cpuFreq
      }),
      Probe({
        title: 'Board voltages & frequencies',
        img: 'rec_board_voltage.png',
        descr: 'Tracks voltage and frequency changes from board sensors',
        setEnabled: (cfg, val) => cfg.boardSensors = val,
        isEnabled: (cfg) => cfg.boardSensors
      }));
}

function CpuSettings(cssClass: string) {
  return m(
      `.record-section${cssClass}`,
      Probe(
          {
            title: 'Coarse CPU usage counter',
            img: 'rec_cpu_coarse.png',
            descr: `Lightweight polling of CPU usage counters via /proc/stat.
                    Allows to periodically monitor CPU usage.`,
            setEnabled: (cfg, val) => cfg.cpuCoarse = val,
            isEnabled: (cfg) => cfg.cpuCoarse
          },
          Slider({
            title: 'Poll rate',
            cssClass: '.thin',
            values: POLL_RATE_MS,
            unit: 'ms',
            set: (cfg, val) => cfg.cpuCoarsePollMs = val,
            get: (cfg) => cfg.cpuCoarsePollMs
          })),
      Probe({
        title: 'Scheduling details',
        img: 'rec_cpu_fine.png',
        descr: 'Enables high-detailed tracking of scheduling events',
        setEnabled: (cfg, val) => cfg.cpuSched = val,
        isEnabled: (cfg) => cfg.cpuSched
      }),
      Probe({
        title: 'Scheduling chains / latency analysis',
        img: 'rec_cpu_wakeup.png',
        descr: `Tracks causality of scheduling transitions. When a task
                  X transitions from blocked -> runnable, keeps track of the
                  task Y that X's transition (e.g. posting a semaphore).`,
        setEnabled: (cfg, val) => cfg.cpuLatency = val,
        isEnabled: (cfg) => cfg.cpuLatency
      }));
}

function MemorySettings(cssClass: string) {
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
      `.record-section${cssClass}`,
      Probe(
          {
            title: 'Kernel meminfo',
            img: 'rec_meminfo.png',
            descr: 'Polling of /proc/meminfo',
            setEnabled: (cfg, val) => cfg.meminfo = val,
            isEnabled: (cfg) => cfg.meminfo
          },
          Slider({
            title: 'Poll rate',
            cssClass: '.thin',
            values: POLL_RATE_MS,
            unit: 'ms',
            set: (cfg, val) => cfg.meminfoPeriodMs = val,
            get: (cfg) => cfg.meminfoPeriodMs
          }),
          Dropdown({
            title: 'Select counters',
            cssClass: '.multicolumn',
            options: meminfoOpts,
            set: (cfg, val) => cfg.meminfoCounters = val,
            get: (cfg) => cfg.meminfoCounters
          })),
      Probe({
        title: 'High-frequency memory events',
        img: 'rec_mem_hifreq.png',
        descr: `Allows to track short memory spikes and transitories through
                  ftrace's mm_event, rss_stat and ion events. Avialable only
                  on recent Android Q+ kernels`,
        setEnabled: (cfg, val) => cfg.memHiFreq = val,
        isEnabled: (cfg) => cfg.memHiFreq
      }),
      Probe({
        title: 'Low memory killer',
        img: 'rec_lmk.png',
        descr: `Record LMK events. Works both with the old in-kernel LMK
                  and the newer userspace lmkd. It also tracks OOM score
                  adjustments.`,
        setEnabled: (cfg, val) => cfg.memLmk = val,
        isEnabled: (cfg) => cfg.memLmk
      }),
      Probe(
          {
            title: 'Per process stats',
            img: 'rec_ps_stats.png',
            descr: `Periodically samples all processes in the system tracking:
                      their thread list, memory counters (RSS, swap and other
                      /proc/status counters) and oom_score_adj.`,
            setEnabled: (cfg, val) => cfg.procStats = val,
            isEnabled: (cfg) => cfg.procStats
          },
          Slider({
            title: 'Poll rate',
            cssClass: '.thin',
            values: POLL_RATE_MS,
            unit: 'ms',
            set: (cfg, val) => cfg.procStatsPeriodMs = val,
            get: (cfg) => cfg.procStatsPeriodMs
          })),
      Probe(
          {
            title: 'Virtual memory stats',
            img: 'rec_vmstat.png',
            descr: `Periodically polls virtual memory stats from /proc/vmstat.
                      Allows to gather statistics about swap, eviction,
                      compression and pagecache efficiency`,
            setEnabled: (cfg, val) => cfg.vmstat = val,
            isEnabled: (cfg) => cfg.vmstat
          },
          Slider({
            title: 'Poll rate',
            cssClass: '.thin',
            values: POLL_RATE_MS,
            unit: 'ms',
            set: (cfg, val) => cfg.vmstatPeriodMs = val,
            get: (cfg) => cfg.vmstatPeriodMs
          }),
          Dropdown({
            title: 'Select counters',
            cssClass: '.multicolumn',
            options: vmstatOpts,
            set: (cfg, val) => cfg.vmstatCounters = val,
            get: (cfg) => cfg.vmstatCounters
          })));
}


function AndroidSettings(cssClass: string) {
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

  const LOG_BUFFERS = new Map<string, string>();
  LOG_BUFFERS.set('LID_RADIO', 'Radio');
  LOG_BUFFERS.set('LID_EVENTS', 'Binary events');
  LOG_BUFFERS.set('LID_SYSTEM', 'System');
  LOG_BUFFERS.set('LID_CRASH', 'Crash');
  LOG_BUFFERS.set('LID_SECURITY', 'Security');
  LOG_BUFFERS.set('LID_KERNEL', 'Kernel');

  return m(
      `.record-section${cssClass}`,
      Probe(
          {
            title: 'Atrace userspace annotations',
            img: 'rec_atrace.png',
            descr: `Enables C++ / Java codebase annotations (ATRACE_BEGIN() /
                    os.Trace())`,
            setEnabled: (cfg, val) => cfg.atrace = val,
            isEnabled: (cfg) => cfg.atrace
          },
          Dropdown({
            title: 'Categories',
            cssClass: '.multicolumn.atrace_categories',
            options: ATRACE_CATEGORIES,
            set: (cfg, val) => cfg.atraceCats = val,
            get: (cfg) => cfg.atraceCats
          }),
          Textarea({
            placeholder: 'Extra apps to profile, one per line, e.g.:\n' +
                'com.android.phone\n' +
                'com.android.nfc',
            set: (cfg, val) => cfg.atraceApps = val,
            get: (cfg) => cfg.atraceApps
          }),  //
          ),
      Probe(
          {
            title: 'Event log (logcat)',
            img: 'rec_logcat.png',
            descr: `Streams the event log into the trace. If no buffer filter is
                  specified, all buffers are selected.`,
            setEnabled: (cfg, val) => cfg.androidLogs = val,
            isEnabled: (cfg) => cfg.androidLogs
          },
          Dropdown({
            title: 'Buffers',
            options: LOG_BUFFERS,
            set: (cfg, val) => cfg.androidLogBuffers = val,
            get: (cfg) => cfg.androidLogBuffers
          }),

          ));
}


function AdvancedSettings(cssClass: string) {
  const FTRACE_CATEGORIES = new Map<string, string>();
  FTRACE_CATEGORIES.set('binder/*', 'binder');
  FTRACE_CATEGORIES.set('block/*', 'block');
  FTRACE_CATEGORIES.set('clk/*', 'clk');
  FTRACE_CATEGORIES.set('ext4/*', 'ext4');
  FTRACE_CATEGORIES.set('f2fs/*', 'f2fs');
  FTRACE_CATEGORIES.set('i2c/*', 'i2c');
  FTRACE_CATEGORIES.set('irq/*', 'irq');
  FTRACE_CATEGORIES.set('kmem/*', 'kmem');
  FTRACE_CATEGORIES.set('memory_bus/*', 'memory_bus');
  FTRACE_CATEGORIES.set('mmc/*', 'mmc');
  FTRACE_CATEGORIES.set('oom/*', 'oom');
  FTRACE_CATEGORIES.set('power/*', 'power');
  FTRACE_CATEGORIES.set('regulator/*', 'regulator');
  FTRACE_CATEGORIES.set('sched/*', 'sched');
  FTRACE_CATEGORIES.set('sync/*', 'sync');
  FTRACE_CATEGORIES.set('task/*', 'task');
  FTRACE_CATEGORIES.set('task/*', 'task');
  FTRACE_CATEGORIES.set('vmscan/*', 'vmscan');

  return m(
      `.record-section${cssClass}`,
      Probe(
          {
            title: 'Advanced ftrace config',
            img: 'rec_ftrace.png',
            descr: `Tunes the kernel-tracing (ftrace) module and allows to
                      enable extra events. The events enabled here are on top
                      of the ones derived when enabling the other probes.`,
            setEnabled: (cfg, val) => cfg.ftrace = val,
            isEnabled: (cfg) => cfg.ftrace
          },
          Slider({
            title: 'Buf size',
            cssClass: '.thin',
            values: [512, 1024, 2 * 1024, 4 * 1024, 16 * 1024, 32 * 1024],
            unit: 'KB',
            set: (cfg, val) => cfg.ftraceBufferSizeKb = val,
            get: (cfg) => cfg.ftraceBufferSizeKb
          }),
          Slider({
            title: 'Drain rate',
            cssClass: '.thin',
            values: [100, 250, 500, 1000, 2500, 5000],
            unit: 'ms',
            set: (cfg, val) => cfg.ftraceDrainPeriodMs = val,
            get: (cfg) => cfg.ftraceDrainPeriodMs
          }),
          Dropdown({
            title: 'Event groups',
            cssClass: '.multicolumn.ftrace_events',
            options: FTRACE_CATEGORIES,
            set: (cfg, val) => cfg.ftraceEvents = val,
            get: (cfg) => cfg.ftraceEvents
          }),
          Textarea({
            placeholder: 'Add extra events, one per line, e.g.:\n' +
                'sched/sched_switch\n' +
                'kmem/*',
            set: (cfg, val) => cfg.ftraceExtraEvents = val,
            get: (cfg) => cfg.ftraceExtraEvents
          })));
}

function Instructions(cssClass: string) {
  const data = globals.trackDataStore.get('config') as {
    commandline: string,
    pbtxt: string,
  } | null;

  const pbtx = data ? data.pbtxt : '';
  let cmd = '';
  cmd += 'adb shell perfetto \\\n';
  cmd += '  -c - --txt  # Read config from stdin (text mode) \\\n';
  cmd += '  -o /data/misc/perfetto-traces/trace \\\n';
  cmd += '<<EOF\n\n';
  cmd += pbtx;
  cmd += '\nEOF\n';
  const docUrl = '//docs.perfetto.dev/#/build-instructions?id=get-the-code';


  const notes: m.Children = [];
  const doc =
      m('span', 'Follow the ', m('a', {href: docUrl}, 'instructions here'));

  const msgFeatNotSupported =
      m('div', `Some of the probes are only supported in the
      last version of perfetto running on Android Q+`);

  const msgPerfettoNotSupported =
      m('div', `Perfetto is not supported natively before Android P.`);

  const msgSideload =
      m('div',
        `If you have a rooted device you can sideload the latest version of
         perfetto. `,
        doc);

  const msgLinux =
      m('div', `In order to use perfetto on Linux you need to
      compile it and run from the standalone build. `, doc);

  switch (targetOS) {
    case 'Q':
      break;
    case 'P':
      notes.push(msgFeatNotSupported);
      notes.push(msgSideload);
      break;
    case 'O':
      notes.push(msgPerfettoNotSupported);
      notes.push(msgSideload);
      break;
    case 'L':
      notes.push(msgLinux);
      break;
    default:
  }

  const onOsChange = (os: string) => {
    targetOS = os;
    globals.rafScheduler.scheduleFullRedraw();
  };

  return m(
      `.record-section.instructions${cssClass}`,
      m('header', 'Instructions'),
      m('label',
        'Select target platform',
        m('select',
          {onchange: m.withAttr('value', onOsChange)},
          m('option', {value: 'Q'}, 'Android Q+'),
          m('option', {value: 'P'}, 'Android P'),
          m('option', {value: 'O'}, 'Android O-'),
          m('option', {value: 'L'}, 'Linux desktop'))),
      notes.length > 0 ? m('.note', notes) : [],
      m(CodeSample, {text: cmd, hardWhitespace: true}), );
}

export const RecordPage = createPage({
  view() {
    const SECTIONS: {[property: string]: (cssClass: string) => m.Child} = {
      buffers: RecSettings,
      instructions: Instructions,
      cpu: CpuSettings,
      power: PowerSettings,
      memory: MemorySettings,
      android: AndroidSettings,
      advanced: AdvancedSettings,
    };

    const pages: m.Children = [];
    const routePage = Router.param('p');
    for (const key of Object.keys(SECTIONS)) {
      const cssClass = routePage === key ? '.active' : '';
      pages.push(SECTIONS[key](cssClass));
    }

    return m(
        '.record-page',
        m('.record-container',
          m('.record-menu',
            m('header', 'Trace config'),
            m('ul',
              m('a[href="#!/record?p=buffers"]',
                m(`li${routePage === 'buffers' ? '.active' : ''}`,
                  m('i.material-icons', 'tune'),
                  m('.title', 'Recording settings'),
                  m('.sub', 'Buffer mode, size and duration'))),
              m('a[href="#!/record?p=instructions"]',
                m(`li${routePage === 'instructions' ? '.active' : ''}`,
                  m('i.material-icons.rec', 'fiber_manual_record'),
                  m('.title', 'Start recording'),
                  m('.sub', 'Generate config and instructions'))), ),
            m('header', 'Probes'),
            m('ul',
              m('a[href="#!/record?p=cpu"]',
                m(`li${routePage === 'cpu' ? '.active' : ''}`,
                  m('i.material-icons', 'subtitles'),
                  m('.title', 'CPU'),
                  m('.sub', 'CPU usage, scheduling, wakeups'))),
              m('a[href="#!/record?p=power"]',
                m(`li${routePage === 'power' ? '.active' : ''}`,
                  m('i.material-icons', 'battery_charging_full'),
                  m('.title', 'Power'),
                  m('.sub', 'Battery and other energy counters'))),
              m('a[href="#!/record?p=memory"]',
                m(`li${routePage === 'memory' ? '.active' : ''}`,
                  m('i.material-icons', 'memory'),
                  m('.title', 'Memory'),
                  m('.sub', 'Physical mem, VM, LMK'))),
              m('a[href="#!/record?p=android"]',
                m(`li${routePage === 'android' ? '.active' : ''}`,
                  m('i.material-icons', 'android'),
                  m('.title', 'Android apps & svcs'),
                  m('.sub', 'atrace and logcat'))),
              m('a[href="#!/record?p=advanced"]',
                m(`li${routePage === 'advanced' ? '.active' : ''}`,
                  m('i.material-icons', 'settings'),
                  m('.title', 'Advanced settings'),
                  m('.sub', 'Complicated stuff for wizards'))), )),
          pages));
  }
});
