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

import {TimeAxis} from './time_axis';
import {TimeScale} from './time_scale';

/**
 * Overview timeline with brush selection.
 */
export const OverviewTimeline = {
  oninit() {
    this.timeScale = new TimeScale([0, 1], [0, 0]);
    this.padding = {top: 0, right: 20, bottom: 0, left: 20};
  },
  view({attrs}) {
    this.timeScale.setLimitsPx(0, attrs.width);
    this.timeScale.setLimitsMs(
        attrs.maxVisibleWindowMs.start, attrs.maxVisibleWindowMs.end);

    return m(
        '.overview-timeline',
        {
          style: {
            width: attrs.width.toString() + 'px',
            overflow: 'hidden',
            height: '120px',
            background: '#eef',
            position: 'relative',
          },
        },
        m(TimeAxis, {
          timeScale: this.timeScale,
          contentOffset: this.padding.left,
          visibleWindowMs: attrs.maxVisibleWindowMs,
          width: attrs.width,
        }),
        m('svg.visualization',
          {
            style: {
              width: `${attrs.width}px`,
              height: '100%',
            }
          },
          m('path', {
            stroke: '#000',
            fill: 'none',
            d: `M0 0 L50 80 L200 10 L600 50 L800 20 L900 70 L ${
                                                                attrs.width
                                                              } 30`,
          })),
        m('.brushes',
          {
            style: {
              position: 'absolute',
              left: `${this.padding.left}px`,
              top: '41px',
              width: '100%',
              height: 'calc(100% - 41px)',
            }
          },
          m('.brush-left .brush', {
            style: {
              background: 'rgba(210,210,210,0.7)',
              position: 'absolute',
              'pointer-events': 'none',
              top: '0',
              height: '100%',
              left: '0',
              width: `${this.timeScale.msToPx(attrs.visibleWindowMs.start)}px`,
            }
          }),
          m('.brush-right .brush', {
            style: {
              background: 'rgba(210,210,210,0.7)',
              position: 'absolute',
              'pointer-events': 'none',
              top: '0',
              height: '100%',
              left: `${this.timeScale.msToPx(attrs.visibleWindowMs.end)}px`,
              width: `${
                        attrs.width -
                        this.timeScale.msToPx(attrs.visibleWindowMs.end)
                      }px`,
            }
          })));
  },
} as
    m.Component<
        {
          visibleWindowMs: {start: number, end: number},
          maxVisibleWindowMs: {start: number, end: number},
          width: number,
        },
        {
          timeScale: TimeScale,
          padding: {top: number, right: number, bottom: number, left: number}
        }>;