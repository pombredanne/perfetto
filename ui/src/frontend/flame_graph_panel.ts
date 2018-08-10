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

import {executeQuery} from '../common/actions';
import {QueryResponse} from '../common/queries';
import {EngineConfig} from '../common/state';

import {globals} from './globals';
import {Panel} from './panel';

interface FlameNode {
  name: string;
  totalTimeMs: number;
  calls: number;
  children: FlameNode[];
  parent: FlameNode|null;
  depth: number;
  hue: number;
}

const FLAME_CHART_QUERY_ID = 'flame_chart_query';
const paddingTop = 25;

export class FlameGraphPanel implements Panel {
  private root: FlameNode|null = null;
  private domStatus: 'notRendered'|'rendered'|'listenersAdded' = 'notRendered';
  private dataStatus: 'waitingForData'|'queryExecuted'|'dataParsed' =
      'waitingForData';
  private queryResponse: QueryResponse|null = null;
  private flameNodePositions: Array<{
    node: FlameNode,
    x: number,
    y: number,
    width: number,
    height: number,
  }> = [];
  private contentRect: ClientRect = {
    left: 0,
    top: 0,
    width: 0,
    height: 0,
    right: 0,
    bottom: 0,
  };
  private hoveredNode: FlameNode|null = null;
  private ctx: CanvasRenderingContext2D|null = null;

  renderCanvas(ctx: CanvasRenderingContext2D) {
    this.ctx = ctx;
    this.loadData();
    this.parseData();

    if (!this.root) {
      return;
    }

    this.flameNodePositions = [];
    this.renderNode(ctx, this.root, 0, this.contentRect.width);
  }

  private loadData() {
    const engine: EngineConfig = globals.state.engines['0'];
    if (engine && engine.ready && this.dataStatus === 'waitingForData') {
      const query = 'select * from slices limit 1000';
      globals.dispatch(executeQuery(engine.id, FLAME_CHART_QUERY_ID, query));
      this.dataStatus = 'queryExecuted';
    }
    const resp =
        globals.queryResults.get(FLAME_CHART_QUERY_ID) as QueryResponse;
    if (resp !== this.queryResponse) {
      this.queryResponse = resp;
    }
  }

  private renderNode(
      ctx: CanvasRenderingContext2D, node: FlameNode, x: number,
      width: number) {
    if (!this.queryResponse) {
      return;
    }
    const maxDepth =
        Math.max(...this.queryResponse.rows.map(r => Number(r.depth))) + 1;
    const heightPerLevel = this.contentRect.height / (maxDepth + 1);
    const y = this.contentRect.height + paddingTop -
        Math.round((node.depth + 2) * heightPerLevel);
    this.flameNodePositions.push(
        {node, x, y: y - paddingTop, width, height: heightPerLevel});

    const hue = node === this.hoveredNode ? '120' : node.hue;
    ctx.fillStyle = `hsl(${hue}, 100%, 55%)`;
    ctx.strokeStyle = '#999';
    ctx.rect(x, y, width, heightPerLevel);
    ctx.fill();
    ctx.stroke();

    ctx.beginPath();
    ctx.fillStyle = '#000';
    ctx.textAlign = 'center';
    let label = node.name;
    let labelWidth = ctx.measureText(label).width;
    while (labelWidth >= width && label !== '..') {
      label = label.substr(0, label.length - 3) + '..';
      labelWidth = ctx.measureText(label).width;
    }
    if (labelWidth < width) {
      ctx.fillText(label, x + width / 2, y + 15);
      ctx.fill();
    }

    let childX = x;

    for (const child of node.children) {
      const percentage = child.totalTimeMs / node.totalTimeMs;
      const childWidth = percentage * width;
      this.renderNode(ctx, child, childX, childWidth);

      childX += childWidth;
    }
  }

  private onMouseMove(e: MouseEvent) {
    if (!this.ctx) {
      return;
    }
    const x = e.clientX - this.contentRect.left;
    const y = e.clientY - this.contentRect.top;

    const match = this.flameNodePositions.filter(position => {
      return position.x <= x && position.x + position.width >= x &&
          position.y <= y && position.y + position.height >= y;
    });
    this.hoveredNode = match.length !== 1 ? null : match[0].node;
    this.renderCanvas(this.ctx);
  }

  private parseData() {
    if (!this.queryResponse || this.dataStatus === 'dataParsed') {
      return;
    }
    this.dataStatus = 'dataParsed';

    this.root = {
      name: 'All',
      totalTimeMs: 0,
      calls: 0,
      children: [],
      parent: null,
      depth: -1,
      hue: 23 + Math.random() * 23,
    };
    const stackToNode = new Map<string|number, FlameNode>();
    stackToNode.set(0, this.root);

    const maxDepth =
        Math.max(...this.queryResponse.rows.map(r => Number(r.depth))) + 1;

    for (let depth = 0; depth < maxDepth; depth++) {
      const slices = this.queryResponse.rows.filter(r => r.depth === depth);

      for (let i = 0; i < slices.length; i++) {
        const slice = slices[i];
        let node: FlameNode|null|undefined = stackToNode.get(slice.stack_id);
        if (!node) {
          const parent = stackToNode.get(slice.parent_stack_id);
          if (!parent) {
            throw Error(`Parent Slice not found: ${slice.parent_stack_id}`);
          }

          node = {
            name: String(slice.name),
            totalTimeMs: 0,
            calls: 0,
            children: [],
            parent,
            depth: Number(slice.depth),
            hue: 23 + Math.random() * 23,
          };

          parent.children.push(node);
          stackToNode.set(slice.stack_id, node);
        }

        while (node) {
          node.calls++;
          node.totalTimeMs += Number(slice.dur);
          node = node.parent;
        }
      }
    }
  }

  updateDom(dom: Element) {
    if (this.domStatus === 'rendered') {
      this.domStatus = 'listenersAdded';

      // TODO detach event listeners on destroy.
      const content = dom.getElementsByClassName('flame-graph-content')[0];
      if (!content) {
        throw new Error('Could not find flame graph elements.');
      }
      this.contentRect = content.getBoundingClientRect();
      content.addEventListener('mousemove', this.onMouseMove.bind(this));
      return;
    }

    dom.innerHTML = `<div class="flame-graph-wrap">
        <header>Flame Graph</Header>
        <div class="flame-graph-content"></div>
        <div class="tooltip"></div>
      </div>`;
    this.domStatus = 'rendered';
  }

  getHeight() {
    return 500;
  }
}
