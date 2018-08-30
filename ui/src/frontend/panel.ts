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

export abstract class Panel<Attrs = {}> implements
    m.Component<Attrs, Panel<Attrs>> {
  private height = 0;
  private isRenderingCanvas = false;

  getHeight() {
    return this.height;
  }

  setHeight(height: number) {
    if (this.isRenderingCanvas) {
      throw Error(
          'Cannot change height while rendering canvas. ' +
          'Consider setting height in the view method.');
    }
    this.height = height;
    // Do a full redraw after changing height to keep DOM and canvas in sync.
    globals.rafScheduler.scheduleFullRedraw();
  }

  renderCanvasInternal(
      ctx: CanvasRenderingContext2D, vnode: PanelVNode<Attrs>) {
    this.isRenderingCanvas = true;
    this.renderCanvas(ctx, vnode);
    this.isRenderingCanvas = false;
  }
  abstract renderCanvas(
      ctx: CanvasRenderingContext2D, vnode: PanelVNode<Attrs>): void;
  abstract view(vnode: m.Vnode<Attrs, this>): m.Children|null|void;
}


export interface PanelVNode<Attrs = {}> extends m.Vnode<Attrs, Panel<Attrs>> {
  tag: {getInitialHeight?: (vnode: PanelVNode<Attrs>) => number}&
      m.Vnode<Attrs, Panel<Attrs>>['tag'];
}

export function assertIsPanel(vnode: m.Vnode): PanelVNode {
  const tag = vnode.tag as {};
  if (typeof tag === 'function' && 'prototype' in tag &&
      tag.prototype instanceof Panel) {
    return vnode as PanelVNode;
  }

  throw Error('This is not a panel vnode.');
}
