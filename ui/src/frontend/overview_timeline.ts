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
 * Overview timeline with a brush for time-based selections.
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
          m(HorizontalBrushSelection, {
            onBrushedPx: (startPx: number, endPx: number) => {
              attrs.onBrushedMs(
                  this.timeScale.pxToMs(startPx), this.timeScale.pxToMs(endPx));
            },
            selectionPx: {
              start: this.timeScale.msToPx(attrs.visibleWindowMs.start),
              end: this.timeScale.msToPx(attrs.visibleWindowMs.end)
            },
          })));
  },
} as
    m.Component<
        {
          visibleWindowMs: {start: number, end: number},
          maxVisibleWindowMs: {start: number, end: number},
          width: number,
          onBrushedMs: (start: number, end: number) => void,
        },
        {
          timeScale: TimeScale,
          padding: {top: number, right: number, bottom: number, left: number},
        }>;

/**
 * Interactive horizontal brush for pixel-based selections.
 */
const HorizontalBrushSelection = {
  oninit() {
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
        this.onBrushedPx(e.clientX - this.offsetLeft, this.selectionPx.end);
      }
      if (draggingEnd) {
        this.onBrushedPx(this.selectionPx.start, e.clientX - this.offsetLeft);
      }
      e.stopPropagation();
    };
    this.mouseUpListener = () => {
      draggingStart = false;
      draggingEnd = false;
    };
  },
  oncreate(vnode) {
    const el = vnode.dom as HTMLElement;
    el.addEventListener('mousemove', this.mouseMoveListener);
    document.body.addEventListener('mouseup', this.mouseUpListener);

    this.offsetLeft = (el.getBoundingClientRect() as DOMRect).x;
  },
  onremove(vnode) {
    const el = vnode.dom as HTMLElement;
    el.removeEventListener('mousemove', this.mouseMoveListener);
    document.body.removeEventListener('mouseup', this.mouseUpListener);
  },
  view({attrs}) {
    this.onBrushedPx = attrs.onBrushedPx;
    this.selectionPx = attrs.selectionPx;

    return m(
        '.brushes',
        {
          style: {
            width: '100%',
            height: '100%',
          }
        },
        m('.brush-left',
          {
            style: {
              background: 'rgba(210,210,210,0.7)',
              position: 'absolute',
              'pointer-events': 'none',
              'border-right': '1px solid #aaa',
              top: '0',
              height: '100%',
              left: '0',
              width: `${attrs.selectionPx.start}px`,
            }
          },
          m(BrushHandle, {
            left: attrs.selectionPx.start,
            onMouseDown: this.leftHandleMouseDownListener
          })),
        m('.brush-right',
          {
            style: {
              background: 'rgba(210,210,210,0.7)',
              position: 'absolute',
              'pointer-events': 'none',
              top: '0',
              height: '100%',
              'border-left': '1px solid #aaa',
              left: `${attrs.selectionPx.end}px`,
              width: `calc(100% - ${attrs.selectionPx.end}px)`,
            }
          },
          m(BrushHandle,
            {left: 0, onMouseDown: this.rightHandleMouseDownListener})));
  }
} as
    m.Component<
        {
          onBrushedPx: (start: number, end: number) => void,
          selectionPx: {start: number, end: number},
        },
        {
          rightHandleMouseDownListener: (e: MouseEvent) => void,
          leftHandleMouseDownListener: (e: MouseEvent) => void,
          mouseMoveListener: (e: MouseEvent) => void,
          mouseUpListener: () => void,
          selectionPx: {start: number, end: number},
          onBrushedPx: (start: number, end: number) => void,
          offsetLeft: number
        }>;

/**
 * Creates a visual handle with three horizontal bars.
 */
const BrushHandle = {
  oncreate(vnode) {
    const el = vnode.dom as HTMLElement;
    el.addEventListener('mousedown', this.boundOnMouseDown);
  },
  onremove(vnode) {
    const el = vnode.dom as HTMLElement;
    el.removeEventListener('mousedown', this.boundOnMouseDown);
  },
  view({attrs}) {
    if (!this.boundOnMouseDown) {
      this.boundOnMouseDown = attrs.onMouseDown.bind(this);
    }

    const handleBar = m('.handle-bar', {
      style: {
        height: '5px',
        width: '8px',
        'margin-left': '2px',
        'border-top': '1px solid #888',
      }
    });

    return m(
        '.brush-handle',
        {
          style: {
            position: 'absolute',
            left: `${attrs.left - 6}px`,
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
        m('.handle-bars',
          {
            style: {
              position: 'relative',
              top: '9px',
            }
          },
          handleBar,
          handleBar,
          handleBar));
  }
} as m.Component<{
  left: number,
  onMouseDown: (e: MouseEvent) => void,
},
                    {
                      boundOnMouseDown: (e: MouseEvent) => void,
                    }>;