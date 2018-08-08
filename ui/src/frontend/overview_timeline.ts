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

import {QueryResponse} from '../common/queries';

import {Animation} from './animation';
import {DragGestureHandler} from './drag_gesture_handler';
import {globals} from './globals';
import {TimeAxis} from './time_axis';
import {TimeScale} from './time_scale';
import {OVERVIEW_QUERY_ID} from './viewer_page';

interface ProcessSummaryData {
  upid: number;
  name: string;
  loadByTime: {[time: number]: number};
  hue: number;
}

const SATURATION = 70;
// The bigger load the darker.
const LIGHTNESS_MIN_LOAD = 80;
const LIGHTNESS_MAX_LOAD = 20;

/**
 * Overview timeline with a brush for time-based selections.
 */
export const OverviewTimeline = {
  oninit() {
    this.timeScale = new TimeScale([0, 1], [0, 0]);
    this.padding = {top: 0, right: 20, bottom: 0, left: 20};

    this.onmousemove = e => {
      if (!this.processesById || !e.target ||
          (e.target as Element).className !== 'brushes') {
        return;
      }
      const y = e.layerY;
      const processes = Object.values(this.processesById);
      const heightPerProcess = this.contentHeight / processes.length;
      const index = Math.floor(y / heightPerProcess);
      this.hoveredProcess = processes[index];
      this.hoveredMs = this.timeScale.pxToMs(e.layerX);
    };
  },
  oncreate(vnode) {
    const rect = vnode.dom.getBoundingClientRect();

    this.contentWidth = rect.width - this.padding.left - this.padding.right;
    this.contentHeight = rect.height - 41;

    this.timeScale.setLimitsPx(this.padding.left, this.contentWidth);

    const context =
        vnode.dom.getElementsByTagName('canvas')[0].getContext('2d');
    if (!context) {
      throw Error('Overview canvas context not found.');
    }
    this.context = context;
  },
  onupdate(vnode) {
    const rect = vnode.dom.getBoundingClientRect();

    this.timeScale.setLimitsPx(
        this.padding.left, rect.width - this.padding.left - this.padding.right);
  },
  view({attrs}) {
    this.timeScale.setLimitsMs(
        attrs.maxVisibleWindowMs.start, attrs.maxVisibleWindowMs.end);

    const resp = globals.queryResults.get(OVERVIEW_QUERY_ID) as QueryResponse;

    if (this.context && (resp || this.queryResponse)) {
      // Render canvas

      if (!this.processesById || resp !== this.queryResponse) {
        // Update data
        this.processesById = {};
        this.queryResponse = resp;

        const data = resp.rows;
        const times = data.map(processLoad => processLoad.rts as number);
        const minTime = Math.min(...times);

        for (const processLoad of data) {
          const upid = processLoad.upid as number;
          if (!this.processesById[upid]) {
            this.processesById[upid] = {
              upid,
              name: processLoad.name as string,
              loadByTime: {},
              hue: Math.random() * 360,
            };
          }
          const time = (processLoad.rts as number - minTime) * 1000;
          this.processesById[upid].loadByTime[time] =
              processLoad.load as number;
        }
      }

      const processes = Object.values(this.processesById);
      const heightPerProcess = this.contentHeight / processes.length;
      const roundedHeightPerProcess = Math.round(heightPerProcess);

      for (let i = 0; i < processes.length; i++) {
        const process = processes[i];
        const startY = Math.round(i * heightPerProcess);

        // Add a background behind the hovered process
        this.context.fillStyle =
            process === this.hoveredProcess ? '#eee' : '#fff';
        this.context.fillRect(
            0, startY, this.contentWidth, roundedHeightPerProcess);

        const loadTimes = Object.keys(process.loadByTime)
                              .map(stringTime => Number(stringTime));

        for (const loadTime of loadTimes) {
          const load = process.loadByTime[loadTime];
          const startPx = this.timeScale.msToPx(loadTime - 500);
          const endPx = this.timeScale.msToPx(loadTime + 500);

          this.context.fillStyle =
              `hsl(${process.hue}, ${SATURATION}%, ${
                                                     LIGHTNESS_MIN_LOAD +
                                                     (LIGHTNESS_MAX_LOAD -
                                                      LIGHTNESS_MIN_LOAD) *
                                                         load / 100
                                                   }%)`;
          this.context.fillRect(
              startPx, startY, endPx - startPx, roundedHeightPerProcess);

          // TODO: Add tooltip to the slice neat this.hoveredMs.
        }
      }
      this.context.fill();
    }

    return m(
        '.overview-timeline',
        m(TimeAxis, {
          timeScale: this.timeScale,
          contentOffset: 0,
          visibleWindowMs: attrs.maxVisibleWindowMs,
        }),
        m('.timeline-content',
          {
            onmousemove: this.onmousemove,
            onmouseout: () => this.hoveredProcess = undefined,
            style: {
              position: 'absolute',
              left: `${this.padding.left}px`,
              top: '41px',
              width: `${this.contentWidth}px`,
              height: `${this.contentHeight}px`,
            }
          },
          m('canvas.visualization', {
            width: this.contentWidth,
            height: this.contentHeight,
          }),
          m('.brushes',
            {
              style: {
                width: '100%',
                height: '100%',
                position: 'absolute',
                top: '0',
              }
            },
            m(HorizontalBrushSelection, {
              onBrushedPx: (startPx: number, endPx: number) => {
                attrs.onBrushedMs(
                    this.timeScale.pxToMs(startPx),
                    this.timeScale.pxToMs(endPx));
              },
              selectionPx: {
                start: this.timeScale.msToPx(attrs.visibleWindowMs.start),
                end: this.timeScale.msToPx(attrs.visibleWindowMs.end)
              },
            }))));
  },
} as
    m.Component<
        {
          visibleWindowMs: {start: number, end: number},
          maxVisibleWindowMs: {start: number, end: number},
          onBrushedMs: (start: number, end: number) => void,
        },
        {
          timeScale: TimeScale,
          padding: {top: number, right: number, bottom: number, left: number},
          context: CanvasRenderingContext2D | undefined,
          contentWidth: number,
          contentHeight: number,
          queryResponse: QueryResponse,
          processesById: {[upid: number]: ProcessSummaryData},
          hoveredProcess: ProcessSummaryData | undefined,
          hoveredMs: number,
          onmousemove: (e: MouseEvent) => void,
        }>;

const ZOOM_IN_PERCENTAGE_PER_MS = 0.998;
const ZOOM_OUT_PERCENTAGE_PER_MS = 1 / ZOOM_IN_PERCENTAGE_PER_MS;
const WHEEL_ZOOM_DURATION = 200;

/**
 * Interactive horizontal brush for pixel-based selections.
 */
const HorizontalBrushSelection = {
  oninit() {
    this.limitThenBrush = (startPx: number, endPx: number) => {
      startPx = Math.min(Math.max(0, startPx), this.width);
      endPx = Math.min(Math.max(0, endPx), this.width);
      this.onBrushedPx(startPx, endPx);
    };
  },
  oncreate(vnode) {
    const el = vnode.dom as HTMLElement;
    const bcr = el.getBoundingClientRect() as DOMRect;
    this.offsetLeft = bcr.x;
    this.width = bcr.width;

    const startHandle =
        el.getElementsByClassName('brush-handle-start')[0] as HTMLElement;
    const endHandle =
        el.getElementsByClassName('brush-handle-end')[0] as HTMLElement;

    let dragState: 'draggingStartHandle'|'draggingEndHandle'|'notDragging' =
        'notDragging';

    const dragged = (posX: number) => {
      if ((dragState === 'draggingEndHandle' &&
           posX < this.selectionPx.start) ||
          (dragState === 'draggingStartHandle' &&
           posX > this.selectionPx.end)) {
        // Flip start and end if handle has been dragged past the other limit.
        dragState = dragState === 'draggingStartHandle' ? 'draggingEndHandle' :
                                                          'draggingStartHandle';
      }
      if (dragState === 'draggingStartHandle') {
        this.limitThenBrush(posX, this.selectionPx.end);
      } else {
        this.limitThenBrush(this.selectionPx.start, posX);
      }
    };

    new DragGestureHandler(
        startHandle,
        x => dragged(x - this.offsetLeft),
        () => dragState = 'draggingStartHandle',
        () => dragState = 'notDragging');
    new DragGestureHandler(
        endHandle,
        x => dragged(x - this.offsetLeft),
        () => dragState = 'draggingEndHandle',
        () => dragState = 'notDragging');

    new DragGestureHandler(el, x => dragged(x - this.offsetLeft), x => {
      this.selectionPx.start = this.selectionPx.end = x - this.offsetLeft;
      dragState = 'draggingEndHandle';
      this.limitThenBrush(this.selectionPx.start, this.selectionPx.end);
    }, () => dragState = 'notDragging');

    this.onMouseMove = e => {
      this.mousePositionX = e.clientX - this.offsetLeft;
    };

    let zoomingIn = true;
    const zoomAnimation = new Animation((timeSinceLastMs: number) => {
      const percentagePerMs =
          zoomingIn ? ZOOM_IN_PERCENTAGE_PER_MS : ZOOM_OUT_PERCENTAGE_PER_MS;
      const percentage = Math.pow(percentagePerMs, timeSinceLastMs);

      const selectionLength = this.selectionPx.end - this.selectionPx.start;
      const newSelectionLength = selectionLength * percentage;

      // Brush toward the mouse, like zooming.
      const zoomPositionPercentage =
          (this.mousePositionX - this.selectionPx.start) / selectionLength;

      const brushStart =
          this.mousePositionX - zoomPositionPercentage * newSelectionLength;
      const brushEnd = this.mousePositionX +
          (1 - zoomPositionPercentage) * newSelectionLength;

      this.limitThenBrush(brushStart, brushEnd);
    });

    this.onWheel = e => {
      if (Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
        // Horizontal motion: panning.
        this.limitThenBrush(
            this.selectionPx.start + e.deltaX, this.selectionPx.end + e.deltaX);
      } else {
        // Vertical motion: zooming.
        zoomingIn = e.deltaY < 0;
        zoomAnimation.start(WHEEL_ZOOM_DURATION);
      }
    };
  },
  onupdate(vnode) {
    const el = vnode.dom as HTMLElement;
    const bcr = el.getBoundingClientRect() as DOMRect;
    this.offsetLeft = bcr.x;
    this.width = bcr.width;
  },
  view({attrs}) {
    this.onBrushedPx = attrs.onBrushedPx;
    this.selectionPx = attrs.selectionPx;

    return m(
        '.brushes',
        {
          onwheel: this.onWheel,
          onmousemove: this.onMouseMove,
          style: {
            width: '100%',
            height: '100%',
          }
        },
        m('.brush-left.brush-rect', {
          style: {
            'border-right': '1px solid #aaa',
            left: '0',
            width: `${attrs.selectionPx.start}px`,
          }
        }),
        m('.brush-right.brush-rect', {
          style: {
            'border-left': '1px solid #aaa',
            left: `${attrs.selectionPx.end}px`,
            width: `calc(100% - ${attrs.selectionPx.end}px)`,
          }
        }),
        m(BrushHandle, {
          left: attrs.selectionPx.start,
          className: 'brush-handle-start',
        }),
        m(BrushHandle, {
          left: attrs.selectionPx.end,
          className: 'brush-handle-end',
        }));
  }
} as
    m.Component<
        {
          onBrushedPx: (start: number, end: number) => void,
          selectionPx: {start: number, end: number},
        },
        {
          selectionPx: {start: number, end: number},
          onBrushedPx: (start: number, end: number) => void,
          offsetLeft: number,
          onWheel: (e: WheelEvent) => void,
          onMouseMove: (e: MouseEvent) => void,
          mousePositionX: number,
          width: number,
          limitThenBrush: (startPx: number, endPx: number) => void,
        }>;

/**
 * Creates a visual handle with three horizontal bars.
 */
const BrushHandle = {
  view({attrs}) {
    const handleBar = m('.handle-bar', {
      style: {
        height: '5px',
        width: '8px',
        'margin-left': '2px',
        'border-top': '1px solid #888',
      }
    });

    return m(
        `.brush-handle.${attrs.className}`,
        {
          style: {
            left: `${attrs.left - 6}px`,
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
  className: string,
},
                    {}>;