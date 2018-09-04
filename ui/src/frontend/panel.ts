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
// import {globals} from './globals';

export abstract class Panel<Attrs = {}> implements
    m.Component<Attrs, Panel<Attrs>> {
  private _height: number|undefined;

  getHeight(): number {
    if (this._height === undefined) {
      throw Error('Attempting to access height before it is computed.');
    }
    return this._height;
  }

  // This method is only for use by the PanelContainer. Panels should not
  // use this.
  _setHeight(height: number) {
    this._height = height;
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
