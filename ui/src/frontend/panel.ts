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

export interface PanelSize {
  width: number;
  height: number;
}

export interface PanelLifecycle<Attrs, State> extends
    m.Lifecycle<Attrs, State> {
  renderCanvas?(this: State, ctx: CanvasRenderingContext2D, size: PanelSize):
      void;
}

export interface Panel<Attrs = {},
                       State extends PanelLifecycle<Attrs, State> =
                                         PanelLifecycle<Attrs, State>> extends
    m.Component<Attrs, State> {
}


export interface PanelVNode<Attrs = {},
                            State extends PanelLifecycle<Attrs, State> =
                                              PanelLifecycle<Attrs, State>>
    extends m.Vnode<Attrs, State> {}

export function assertIsPanel(vnode: m.Vnode): PanelVNode {
  const tag = vnode.tag as {};
  if (typeof tag === 'function' && 'prototype' in tag &&
      tag.prototype.renderCanvas !== undefined) {
    return vnode as PanelVNode;
  }
  throw Error('This is not a panel vnode.');
}
