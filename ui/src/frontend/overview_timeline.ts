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
import {Vnode} from 'mithril';

import {TimeAxis} from './time_axis';
import {TimeScale} from './time_scale';

/**
 * Overview timeline with brush selection.
 */
export const OverviewTimeline = {
  oninit() {
    this.timeScale = new TimeScale([0, 1], [0, 0]);
    this.padding = {top: 0, right: 20, bottom: 0, left: 20};

    const handleBar = m('.handle-bar', {
      style: {
        height: '5px',
        width: '8px',
        'margin-left': '2px',
        'border-top': '1px solid #888',
      }
    });
    this.handleBars =
        m('.handle-bars',
          {
            style: {
              position: 'relative',
              top: '9px',
            }
          },
          handleBar,
          handleBar,
          handleBar);
  },
  oncreate(vnode) {
    const el = vnode.dom as HTMLElement;
    const leftHandle =
        el.getElementsByClassName('brush-left-handle')[0] as HTMLElement;
    const rightHandle =
        el.getElementsByClassName('brush-right-handle')[0] as HTMLElement;

    let draggingStart = false;
    let draggingEnd = false;

    this.rightHandleMouseDownListener = e => {
      draggingEnd = true;
      e.stopPropagation();
    };
    this.leftHandleMouseDownListener = e => {
      draggingStart = true;
      e.stopPropagation();
    };
    this.mouseMoveListener = (e: MouseEvent) => {
      if (draggingStart) {
        const start = this.timeScale.pxToMs(e.clientX - this.padding.left);
        this.onBrushed(start, this.visibleWindowMs.end);
      }
      if (draggingEnd) {
        const end = this.timeScale.pxToMs(e.clientX - this.padding.left);
        this.onBrushed(this.visibleWindowMs.start, end);
      }
      e.stopPropagation();
    };
    this.mouseUpListener = () => {
      draggingStart = false;
      draggingEnd = false;
    };

    leftHandle.addEventListener('mousedown', this.leftHandleMouseDownListener);
    rightHandle.addEventListener(
        'mousedown', this.rightHandleMouseDownListener);

    el.addEventListener('mousemove', this.mouseMoveListener);
    document.body.addEventListener('mouseup', this.mouseUpListener);
  },
  onremove(vnode) {
    const el = vnode.dom as HTMLElement;
    const leftHandle =
        el.getElementsByClassName('brush-left-handle')[0] as HTMLElement;
    const rightHandle =
        el.getElementsByClassName('brush-right-handle')[0] as HTMLElement;

    leftHandle.removeEventListener(
        'mousedown', this.leftHandleMouseDownListener);
    rightHandle.removeEventListener(
        'mousedown', this.rightHandleMouseDownListener);

    el.addEventListener('mousemove', this.mouseMoveListener);
    document.body.addEventListener('mouseup', this.mouseUpListener);
  },
  view({attrs}) {
    this.timeScale.setLimitsPx(0, attrs.width);
    this.timeScale.setLimitsMs(
        attrs.maxVisibleWindowMs.start, attrs.maxVisibleWindowMs.end);
    this.visibleWindowMs = attrs.visibleWindowMs;
    this.onBrushed = attrs.onBrushed;
    // a b c

    return m(
        '.overview-timeline',
        {
          style: {
            width: attrs.width.toString() + 'px',
            overflow: 'hidden',
            height: '120px',
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
            d: `M${this.padding.left} 0 L50 80 L200 10 L600 50 L800 20 ` +
                `L900 70L ${attrs.width - this.padding.right} 30`,
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
          m('.brush-left',
            {
              style: {
                background: 'rgba(210,210,210,0.7)',
                position: 'absolute',
                'pointer-events': 'none',
                top: '0',
                height: '100%',
                left: '0',
                width:
                    `${this.timeScale.msToPx(attrs.visibleWindowMs.start)}px`,
              }
            },
            m('.brush-left-handle',
              {
                style: {
                  position: 'absolute',
                  left:
                      `${
                         this.timeScale.msToPx(attrs.visibleWindowMs.start) - 6
                       }px`,
                  'border-radius': '3px',
                  border: '1px solid #999',
                  cursor: 'pointer',
                  background: '#fff',
                  top: '25px',
                  width: '14px',
                  height: '30px',
                  'pointer-events': 'auto',
                }
              },
              this.handleBars)),
          m('.brush-right',
            {
              style: {
                background: 'rgba(210,210,210,0.7)',
                position: 'absolute',
                'pointer-events': 'none',
                top: '0',
                height: '100%',
                left: `${this.timeScale.msToPx(attrs.visibleWindowMs.end)}px`,
                width: `${
                          attrs.width - this.padding.left - this.padding.right -
                          this.timeScale.msToPx(attrs.visibleWindowMs.end)
                        }px`,
              }
            },
            m('.brush-right-handle',
              {
                style: {
                  position: 'absolute',
                  left: '-6px',
                  'border-radius': '3px',
                  border: '1px solid #999',
                  cursor: 'pointer',
                  background: '#fff',
                  top: '25px',
                  width: '14px',
                  height: '30px',
                  'pointer-events': 'auto',
                }
              },
              this.handleBars))));
  },
} as
    m.Component<
        {
          visibleWindowMs: {start: number, end: number},
          maxVisibleWindowMs: {start: number, end: number},
          width: number,
          onBrushed: (start: number, end: number) => void,
        },
        {
          timeScale: TimeScale,
          padding: {top: number, right: number, bottom: number, left: number},
          // tslint:disable no-any
          handleBars: Vnode<any, any>,
          visibleWindowMs: {start: number, end: number},
          onBrushed: (start: number, end: number) => void,
          rightHandleMouseDownListener: (e: MouseEvent) => void,
          leftHandleMouseDownListener: (e: MouseEvent) => void,
          mouseMoveListener: (e: MouseEvent) => void,
          mouseUpListener: () => void,
        }>;