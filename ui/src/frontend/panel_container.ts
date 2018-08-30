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

import {assertExists} from '../base/logging';

import {globals} from './globals';
import {assertIsPanel, PanelVNode} from './panel';


/**
 * If the panel container scrolls, the backing canvas height is
 * SCROLLING_CANVAS_OVERDRAW_FACTOR * parent container height.
 */
const SCROLLING_CANVAS_OVERDRAW_FACTOR = 2;

function getCanvasOverdrawHeightPerSide(vnode: PanelContainerVnode) {
  const overdrawHeight =
      (vnode.state.canvasOverdrawFactor - 1) * vnode.state.parentHeight;
  return overdrawHeight / 2;
}

function updateCanvasDimensions(vnodeDom: PanelContainerVnodeDom) {
  const canvas =
      assertExists(vnodeDom.dom.querySelector('canvas.main-canvas')) as
      HTMLCanvasElement;
  const ctx = assertExists(vnodeDom.state.ctx);
  canvas.style.height = `${vnodeDom.state.canvasHeight}px`;
  const dpr = window.devicePixelRatio;
  ctx.canvas.width = vnodeDom.state.parentWidth * dpr;
  ctx.canvas.height = vnodeDom.state.canvasHeight * dpr;
  ctx.scale(dpr, dpr);
}

function panelIsOnCanvas(
    panelYBoundsOnCanvas: {start: number, end: number}, canvasHeight: number) {
  return panelYBoundsOnCanvas.end > 0 &&
      panelYBoundsOnCanvas.start < canvasHeight;
}


function renderPanelCanvas(
    ctx: CanvasRenderingContext2D,
    width: number,
    yStartOnCanvas: number,
    panel: PanelVNode) {
  ctx.save();
  ctx.translate(0, yStartOnCanvas);
  const clipRect = new Path2D();
  clipRect.rect(0, 0, width, panel.state.getHeight());
  ctx.clip(clipRect);
  panel.state.renderCanvasInternal(ctx, panel);

  ctx.restore();
}

function redrawAllPanelCavases(vnode: PanelContainerVnode) {
  const state = vnode.state;
  if (!state.ctx) return;
  const totalHeight = vnode.state.panels.reduce(
      (sum, panel) => sum + panel.state.getHeight(), 0);
  const canvasHeight = totalHeight;
  state.ctx.clearRect(0, 0, state.parentWidth, canvasHeight);
  const canvasYStart = state.scrollTop - getCanvasOverdrawHeightPerSide(vnode);

  let panelYStart = 0;
  for (const panel of vnode.state.panels) {
    const yStartOnCanvas = panelYStart - canvasYStart;
    const panelHeight: number = panel.state.getHeight();
    const panelYBoundsOnCanvas = {
      start: yStartOnCanvas,
      end: yStartOnCanvas + panelHeight,
    };
    if (!panelIsOnCanvas(panelYBoundsOnCanvas, canvasHeight)) {
      panelYStart += panelHeight;
      continue;
    }

    renderPanelCanvas(state.ctx, state.parentWidth, yStartOnCanvas, panel);
    panelYStart += panelHeight;
  }
}

function repositionCanvas(vnodeDom: PanelContainerVnodeDom) {
  const canvas =
      assertExists(vnodeDom.dom.querySelector('canvas.main-canvas')) as
      HTMLCanvasElement;
  const canvasYStart =
      vnodeDom.state.scrollTop - getCanvasOverdrawHeightPerSide(vnodeDom);
  canvas.style.transform = `translateY(${canvasYStart}px)`;
}

function sumPanelHeight(vnode: PanelContainerVnode) {
  return vnode.state.panels.reduce(
      (sum, panel) => sum + panel.state.getHeight(), 0);
}

function getCanvasHeight(vnode: PanelContainerVnode) {
  return vnode.attrs.doesScroll ?
      vnode.state.parentHeight * vnode.state.canvasOverdrawFactor :
      vnode.state.totalPanelHeight;
}

interface PanelContainerState {
  parentWidth: number;
  parentHeight: number;
  scrollTop: number;
  canvasOverdrawFactor: number;
  ctx: CanvasRenderingContext2D|null;
  panels: PanelVNode[];
  totalPanelHeight: number;
  canvasHeight: number;

  // We store these functions so we can remove them.
  onResize: () => void;
  canvasRedrawer: () => void;
  parentOnScroll: () => void;
}

interface PanelContainerAttrs {
  panels: m.Vnode[];
  doesScroll: boolean;
}

// Vnode contains state + attrs. VnodeDom contains state + attrs + dom.
type PanelContainerVnode = m.Vnode<PanelContainerAttrs, PanelContainerState>;
type PanelContainerVnodeDom =
    m.VnodeDOM<PanelContainerAttrs, PanelContainerState>;

const PanelWrapper: m.Component<{panel: PanelVNode}> = {
  view({attrs}) {
    return m('.panel', attrs.panel);
  },

  oncreate({attrs, dom}) {
    (dom as HTMLElement).style.height = `${attrs.panel.state.getHeight()}px`;
  },

  onupdate({attrs, dom}) {
    (dom as HTMLElement).style.height = `${attrs.panel.state.getHeight()}px`;
  }
};

export const PanelContainer = {
  oninit(vnode: PanelContainerVnode) {
    // These values are updated with proper values in oncreate.
    this.parentWidth = 0;
    this.parentHeight = 0;
    this.scrollTop = 0;
    this.canvasOverdrawFactor =
        vnode.attrs.doesScroll ? SCROLLING_CANVAS_OVERDRAW_FACTOR : 1;
    this.ctx = null;
    this.panels = [];
    this.canvasRedrawer = () => redrawAllPanelCavases(vnode);
    globals.rafScheduler.addRedrawCallback(this.canvasRedrawer);
  },

  oncreate(vnodeDom: PanelContainerVnodeDom) {
    // Save the canvas context in the state.
    const canvas =
        vnodeDom.dom.querySelector('.main-canvas') as HTMLCanvasElement;
    const ctx = canvas.getContext('2d');
    if (!ctx) {
      throw Error('Cannot create canvas context');
    }
    this.ctx = ctx;

    const clientRect =
        assertExists(vnodeDom.dom.parentElement).getBoundingClientRect();
    this.parentWidth = clientRect.width;
    this.parentHeight = clientRect.height;

    // Initialize panel heights.
    for (const panel of this.panels) {
      const height =
          panel.tag.getInitialHeight ? panel.tag.getInitialHeight(panel) : 0;
      panel.state.setHeight(height);
    }

    this.totalPanelHeight = sumPanelHeight(vnodeDom);
    (vnodeDom.dom as HTMLElement).style.height = `${this.totalPanelHeight}px`;

    this.canvasHeight = getCanvasHeight(vnodeDom);
    updateCanvasDimensions(vnodeDom);

    // Save the resize handler in the state so we can remove it later.
    // TODO: Encapsulate resize handling better.
    this.onResize = () => {
      const clientRect =
          assertExists(vnodeDom.dom.parentElement).getBoundingClientRect();
      this.parentWidth = clientRect.width;
      this.parentHeight = clientRect.height;
      this.canvasHeight = getCanvasHeight(vnodeDom);
      updateCanvasDimensions(vnodeDom);
      globals.rafScheduler.scheduleFullRedraw();
    };

    // Once ResizeObservers are out, we can stop accessing the window here.
    window.addEventListener('resize', this.onResize);

    if (vnodeDom.attrs.doesScroll) {
      this.parentOnScroll = () => {
        vnodeDom.state.scrollTop = vnodeDom.dom.parentElement!.scrollTop;
        repositionCanvas(vnodeDom);
        globals.rafScheduler.scheduleRedraw();
      };
      vnodeDom.dom.parentElement!.addEventListener(
          'scroll', this.parentOnScroll, {passive: true});
    }
  },

  onremove({attrs, dom}) {
    window.removeEventListener('resize', this.onResize);
    globals.rafScheduler.removeRedrawCallback(this.canvasRedrawer);
    if (attrs.doesScroll) {
      dom.parentElement!.removeEventListener('scroll', this.parentOnScroll);
    }
  },

  view({attrs}) {
    // Cache panels vnodes in state for canvas rendering code can access them.
    this.panels = attrs.panels.map(vnode => assertIsPanel(vnode));

    return m(
        '.scroll-limiter',
        m('canvas.main-canvas'),
        this.panels.map(panel => m(PanelWrapper, {panel})));
  },

  onupdate(vnodeDom: PanelContainerVnodeDom) {
    repositionCanvas(vnodeDom);

    const totalPanelHeight = sumPanelHeight(vnodeDom);
    if (this.totalPanelHeight !== totalPanelHeight) {
      this.totalPanelHeight = totalPanelHeight;
      (vnodeDom.dom as HTMLElement).style.height = `${this.totalPanelHeight}px`;
    }

    // In non-scrolling case, canvas height can change if panel heights changed.
    const canvasHeight = getCanvasHeight(vnodeDom);
    if (this.canvasHeight !== canvasHeight) {
      this.canvasHeight = canvasHeight;
      updateCanvasDimensions(vnodeDom);
    }
  }
} as m.Component<PanelContainerAttrs, PanelContainerState>;
