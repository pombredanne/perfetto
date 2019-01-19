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

import {globals} from './globals';
import {createPage} from './pages';

let dur = 0;
const TIMES =
    [5, 10, 30, 60, 5 * 60, 30 * 60, 1 * 3600, 3 * 3600, 12 * 3600, 24 * 3600];
function setDur(x: number) {
  dur = x;
  globals.rafScheduler.scheduleFullRedraw();
}


let siz = 0;
const SIZES = [2, 5, 10, 100, 1000, 10000];
function setSiz(x: number) {
  siz = x;
  globals.rafScheduler.scheduleFullRedraw();
}

function RecSettings() {
  return m(
      'div',
      m('.container',
        m('.g2',
          m('div',
            m('img.logo[src=assets/rec_ring_buf.png]'),
            m('label', 'Ring buffer'), ),
          m('div',
            m('img.logo[src=assets/rec_long_trace.png]'),
            m('label', 'Long trace'), ), ),

        m('.slider',
          m('header', 'In-memory buffer size'),
          m('i.material-icons', '360'),
          m(`input[id="rec_dur"][type=range][min=0][max=${
                                                          TIMES.length - 1
                                                        }][value=${dur}]`,
            {oninput: m.withAttr('value', setDur)}),
          m('label[for="rec_dur"]', `${TIMES[+dur]} MB`)),

        m('.slider',
          m('header', 'Max duration'),
          m('i.material-icons', 'timer'),
          m(`input[id="rec_dur"][type=range][min=0][max=${
                                                          TIMES.length - 1
                                                        }][value=${dur}]`,
            {oninput: m.withAttr('value', setDur)}),
          m('label[for="rec_dur"]', `${TIMES[+dur]} s.`)),

        m('.slider',
          m('header', 'Max file size'),
          m('i.material-icons', 'save'),
          m(`input[id="rec_size"][type=range][min=0][max=${
                                                           SIZES.length - 1
                                                         }][value=${siz}]`,
            {oninput: m.withAttr('value', setSiz)}),
          m('label[for="rec_size"]', `${SIZES[+siz]} MB`)), ),
      m('.wizard',
        m('.slider',
          m('header', 'Flush on disk every'),
          m('i.material-icons', 'av_timer'),
          m(`input[id="rec_period"][type=range][min=0][max=${
                                                             SIZES.length - 1
                                                           }][value=${siz}]`,
            {oninput: m.withAttr('value', setSiz)}),
          m('label[for="rec_period"]', `${SIZES[+siz]} MB`))));
}


function CpuSettings() {
  return m(
      'div',
      m('.container',
        m('.g2',
          m('div',
            m('img.logo[src=assets/rec_cpu_coarse.png]'),
            m('label', 'Coarse counters'), ),
          m('div',
            m('img.logo[src=assets/rec_cpu_fine.png]'),
            m('label', 'Scheduling details'), ), ), ),
      m('.wizard',
        m('.slider',
          m('header', 'Flush on disk every'),
          m('i.material-icons', 'av_timer'),
          m(`input[id="rec_period"][type=range][min=0][max=${
                                                             SIZES.length - 1
                                                           }][value=${siz}]`,
            {oninput: m.withAttr('value', setSiz)}),
          m('label[for="rec_period"]', `${SIZES[+siz]} MB`))));
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
            m('header', 'Operating system'),
            m('ul',
              m('a[href="#!/record/cpu"]',
                m(`li${routePage === 'cpu' ? '.active' : ''}`,
                  m('i.material-icons', 'subtitles'),
                  m('.title', 'CPU & Scheduling'),
                  m('.sub', 'Process execution, blockages'))),
              m('li', m('i.material-icons', 'memory'), m('.title', 'Memory')),
              m('li',
                m('i.material-icons', 'battery_charging_full'),
                m('.title', 'Power')),
              m('li',
                m('i.material-icons', 'sd_storage'),
                m('.title', 'Disk I/O')),
              m('li',
                m('i.material-icons', 'developer_board'),
                m('.title', 'Peripherals')), ),
            m('header', 'Platform'),
            m('li',
              m('i.material-icons', 'android'),
              m('.title', 'Android framework')),
            m('li',
              m('i.material-icons', 'receipt'),
              m('.title', 'Event logs')), ),
          m('.content', page)));
  }
});
