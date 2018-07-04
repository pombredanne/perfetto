/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import * as m from 'mithril';
import {track} from './track';
//import {canvasWrapper} from './canvas_wrapper';
import {CanvasController} from "./canvas_controller";
import {scrollableContainer} from "./scrollableContainer";

type CanvasWrapper = m.Comp<{
  scrollOffset: number,
  canvasElement: HTMLCanvasElement,
}, {}>;

const canvasWrapper = {
  view({attrs}) {
    console.log(attrs.scrollOffset);
    return m('.canvasWrapper', {
      style: {
        position: 'absolute',
        top: attrs.scrollOffset + 'px',
        overflow: 'none',
      }
    })
  },

  oncreate(vnode) {
    vnode.dom.appendChild(vnode.attrs.canvasElement);
  }
} as CanvasWrapper;


export const frontend = {
  oninit() {

    this.width = 1000;
    this.height = 400;

    this.cc = new CanvasController(this.width, this.height);
  },
  view({}) {
    const canvasScrollOffset = this.cc.getCanvasScrollOffset();

    const cctx = this.cc.getContext();
    cctx.fillStyle = 'white';
    cctx.fillRect(0,0, 1000, 800);
    cctx.fillStyle = 'blue';

    for(let y = 0; y < 800; y += 100) {
      cctx.fillRect(0, y, this.width, 5);
    }

    return m('.frontend',
      {
        style: {
          position: 'relative',
          width: this.width.toString() + 'px'
        }
      },
      m(scrollableContainer,
        {
          width: this.width,
          height: this.height,
          contentHeight: 1000,
          onPassiveScroll: (scrollTop: number) => {
            console.log(scrollTop);
            this.cc.updateScrollOffset(scrollTop);
            this.cc.getContext().setYOffset(scrollTop * -1);
            m.redraw();
          },
        },
        /*m('.canvasDiv', {
          style: {
            height: '10000px'
          }
        })*/
        m(canvasWrapper, {
          scrollOffset: canvasScrollOffset,
          canvasElement: this.cc.getCanvasElement()
        }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 0 }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 100, }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 200, }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 300 }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 400 }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 500 }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 600 }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 700 }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 800 }),
        m(track, { name: 'Track 123', cctx: this.cc.getContext(), top: 900 })
      ),

    );
  },
} as m.Comp<{width: number, height: number}, {
  oninit: () => void,
  cc: CanvasController,
  width: number,
  height: number
}>;
