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
const height = 475;
const paddingTop = 25;

export class FlameGraphPanel implements Panel {
  private renderedDom = false;
  private root: FlameNode|null = null;
  private status: 'waitingForData'|'queryExecuted'|'dataParsed' =
      'waitingForData';
  private queryResponse: QueryResponse|null = null;

  renderCanvas(ctx: CanvasRenderingContext2D) {
    this.loadData();
    this.parseData();

    if (!this.root) {
      return;
    }

    this.renderNode(ctx, this.root, 0, 900);
  }

  private loadData() {
    const engine: EngineConfig = globals.state.engines['0'];
    if (engine && engine.ready && this.status === 'waitingForData') {
      const query = 'select * from slices limit 1000';
      globals.dispatch(executeQuery(engine.id, FLAME_CHART_QUERY_ID, query));
      this.status = 'queryExecuted';
    }
    const resp =
        globals.queryResults.get(FLAME_CHART_QUERY_ID) as QueryResponse;
    if (resp !== this.queryResponse) {
      this.queryResponse = resp;
    }
  }

  private renderNode(
      ctx: CanvasRenderingContext2D, node: FlameNode, startPx: number,
      width: number) {
    if (!this.queryResponse) {
      return;
    }
    const maxDepth =
        Math.max(...this.queryResponse.rows.map(r => Number(r.depth))) + 1;
    const heightPerLevel = (height - paddingTop) / (maxDepth + 1);
    const y = height - Math.round((node.depth + 2) * heightPerLevel);

    ctx.fillStyle = `hsl(${node.hue}, 100%, 55%)`;
    ctx.strokeStyle = '#999';
    ctx.rect(startPx, y, width, heightPerLevel);
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
      ctx.fillText(label, startPx + width / 2, y + 15);
      ctx.fill();
    }

    let x = startPx;

    for (const child of node.children) {
      const percentage = child.totalTimeMs / node.totalTimeMs;
      const childWidth = percentage * width;
      this.renderNode(ctx, child, x, childWidth);

      x += childWidth;
    }
  }

  private parseData() {
    if (!this.queryResponse || this.status === 'dataParsed') {
      return;
    }
    this.status = 'dataParsed';

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
    console.log(this.root);
  }

  updateDom(dom: Element) {
    if (this.renderedDom) return;
    dom.innerHTML = `<header>Flame Graph</Header>`;
    this.renderedDom = true;
  }
}
