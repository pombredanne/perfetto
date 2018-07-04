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

import {TrackCanvasContext} from './track_canvas_context';

const setupCanvasContext = (context?: TrackCanvasContext) => {

  const ctx = context ? context : {} as CanvasRenderingContext2D;
  if(!context) {
    ctx['stroke'] = () => {};
    ctx['beginPath'] = () => {};
    ctx['closePath'] = () => {};
    ctx['measureText'] = () => ({} as TextMetrics);
  }

  const offsetRect = {
    top: 100,
    left: 200,
    width: 500,
    height: 600
  };

  const tcctx = new TrackCanvasContext(ctx, offsetRect);

  return { offsetRect, ctx, tcctx };
};

test('track canvas context offsets work on fillrect', async () => {

  const {offsetRect, ctx, tcctx} = setupCanvasContext();

  ctx.fillRect = (x: number, y: number, width: number, height: number) => {
    expect(x).toEqual(offsetRect.left + rect.left);
    expect(y).toEqual(offsetRect.top + rect.top);
    expect(width).toEqual(rect.width);
    expect(height).toEqual(rect.height);
  };

  const rect = {
    top: 5,
    left: 10,
    width: 100,
    height: 200
  };

  tcctx.fillRect(rect.left, rect.top, rect.width, rect.height);
});

test('track canvas context offsets work on filltext', async () => {

  const {offsetRect, ctx, tcctx} = setupCanvasContext();

  ctx.fillText = (_: string, x: number, y: number) => {
    expect(x).toEqual(offsetRect.left + pos.x);
    expect(y).toEqual(offsetRect.top + pos.y);
  };

  const pos = {
    x: 5,
    y: 10
  };

  tcctx.fillText('', pos.x, pos.y);
});

test('track canvas context offsets work on moveto and lineto', async () => {

  const {offsetRect, ctx, tcctx} = setupCanvasContext();

  const checkPosition = (x: number, y: number) => {
    expect(x).toEqual(offsetRect.left + pos.x);
    expect(y).toEqual(offsetRect.top + pos.y);
  };

  ctx.moveTo = checkPosition;
  ctx.lineTo = checkPosition;

  const pos = {
    x: 5,
    y: 10
  };

  tcctx.moveTo(pos.x, pos.y);
  tcctx.lineTo(pos.x, pos.y);
});

test('track canvas context limits the bbox', async () => {

  const {offsetRect, ctx, tcctx} = setupCanvasContext();
  ctx.fillRect = () => {};

  // Filling the entire rect should work.
  tcctx.fillRect(0, 0, offsetRect.width, offsetRect.height);

  // Too much width should not work.
  expect(() => {
    tcctx.fillRect(0, 0, offsetRect.width + 1, offsetRect.height);
  }).toThrow();

  expect(() => {
    tcctx.fillRect(1, 0, offsetRect.width, offsetRect.height);
  }).toThrow();

  // Being too far to the left should not work.
  expect(() => {
    tcctx.fillRect(-1, 0, offsetRect.width, offsetRect.height);
  }).toThrow();

  // Too much height should not work.
  expect(() => {
    tcctx.fillRect(0, 0, offsetRect.width, offsetRect.height + 1);
  }).toThrow();

  expect(() => {
    tcctx.fillRect(0, 1, offsetRect.width, offsetRect.height);
  }).toThrow();

  // Being too far to the top should not work.
  expect(() => {
    tcctx.fillRect(0, -1, offsetRect.width, offsetRect.height);
  }).toThrow();
});

test('nested track canvas contexts work', async () => {
  const {offsetRect, ctx, tcctx} = setupCanvasContext();
  const nestedContext = setupCanvasContext(tcctx);
  const offsetRect2 = nestedContext.offsetRect;
  const tcctx2 = nestedContext.tcctx;

  const checkPosition = (x: number, y: number) => {
    expect(x).toEqual(offsetRect.left + offsetRect2.left + pos.x);
    expect(y).toEqual(offsetRect.top + offsetRect2.top + pos.y);
  };

  ctx.moveTo = checkPosition;
  ctx.lineTo = checkPosition;

  const pos = {
    x: 5,
    y: 10
  };

  tcctx2.moveTo(pos.x, pos.y);
  tcctx2.lineTo(pos.x, pos.y);
});