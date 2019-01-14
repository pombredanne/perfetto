// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use size file except in compliance with the License.
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

import {Actions} from '../common/actions';

import {globals} from './globals';
import {Panel, PanelSize} from './panel';
import {TRACK_SHELL_WIDTH} from './track_panel';

export class NotesPanel extends Panel {
  hoveredX: null|number = null;

  oncreate({dom}: m.CVnodeDOM) {
    dom.addEventListener('mousemove', (e: Event) => {
      this.hoveredX = (e as MouseEvent).layerX - TRACK_SHELL_WIDTH;
      globals.rafScheduler.scheduleRedraw();
    }, {passive: true});
    dom.addEventListener('mouseenter', (e: Event) => {
      this.hoveredX = (e as MouseEvent).layerX - TRACK_SHELL_WIDTH;
      globals.rafScheduler.scheduleRedraw();
    });
    dom.addEventListener('mouseout', () => {
      this.hoveredX = null;
      globals.rafScheduler.scheduleRedraw();
    }, {passive: true});
  }

  view() {
    return m('.notes-panel', {
      onclick: (e: MouseEvent) => {
        this.onClick(e.layerX - TRACK_SHELL_WIDTH, e.layerY);
      },
    });
  }

  renderCanvas(ctx: CanvasRenderingContext2D, size: PanelSize) {
    ctx.fillStyle = '#000';
    const timeScale = globals.frontendLocalState.timeScale;
    let noteSelected = false;
    for (const note of Object.values(globals.state.notes)) {
      const timestamp = note.timestamp;
      if (!timeScale.timeInBounds(timestamp)) continue;
      const x = timeScale.timeToPx(timestamp);
      const left = Math.floor(x + TRACK_SHELL_WIDTH);
      ctx.fillRect(left, 1, 1, size.height - 1);
      if (!noteSelected && this.hoveredX && x <= this.hoveredX &&
          this.hoveredX < x + 10) {
        noteSelected = true;
        ctx.fillRect(left, 1, 10, Math.ceil(size.height / 3));
      } else {
        ctx.strokeRect(left + 0.5, 1.5, 10, Math.ceil(size.height / 3));
      }
    }
    if (this.hoveredX !== null && !noteSelected) {
      const timestamp = timeScale.pxToTime(this.hoveredX);
      if (timeScale.timeInBounds(timestamp)) {
        const x = timeScale.timeToPx(timestamp);
        ctx.fillRect(Math.floor(x + TRACK_SHELL_WIDTH), 0, 1, size.height);
      }
    }
  }

  private onClick(x: number, _: number) {
    const timeScale = globals.frontendLocalState.timeScale;
    const timestamp = timeScale.pxToTime(x);
    for (const note of Object.values(globals.state.notes)) {
      const noteX = timeScale.timeToPx(note.timestamp);
      if (noteX <= x && x < noteX + 10) {
        globals.dispatch(Actions.removeNote({id: note.id}));
        return;
      }
    }
    globals.dispatch(Actions.addNote({timestamp}));
  }
}
