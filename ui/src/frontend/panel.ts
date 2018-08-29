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

export abstract class Panel<Attrs = {}> implements
    m.Component<Attrs, Panel<Attrs>> {
  // getHeight should take vnode as an arg, because height can depend on
  // vnode.attrs if the panel has any attrs.
  abstract getHeight(vnode: PanelVNode<Attrs>): number;
  abstract renderCanvas(
      ctx: CanvasRenderingContext2D, vnode: PanelVNode<Attrs>): void;
  abstract view(vnode: m.Vnode<Attrs, this>): m.Children|null|void;
}

export interface PanelVNode<Attrs = {}> extends m.Vnode<Attrs, Panel<Attrs>> {}

export function assertIsPanel<Attrs>(vnode: m.Vnode<Attrs>): PanelVNode<Attrs> {
  const tag = vnode.tag as {};
  if (typeof tag === 'function' && 'prototype' in tag &&
      tag.prototype instanceof Panel) {
    return vnode as PanelVNode<Attrs>;
  }

  throw Error('This is not a panel vnode.');
}
