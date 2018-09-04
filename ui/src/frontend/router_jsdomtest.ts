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

import {Action, navigate} from '../common/actions';

import {globals} from './globals';
import {ROUTE_PREFIX, Router} from './router';

const mockComponent = {
  view() {}
};

beforeEach(() => {
  globals.resetForTesting();
  window.onhashchange = null;
  window.location.hash = '';
});

test('Default route must be defined', () => {
  expect(() => new Router('/a', {'/b': mockComponent})).toThrow();
});

test('Returns default component for empty state route', () => {
  globals.initialize({});
  const router = new Router('/a', {'/a': mockComponent});
  globals.state.route = '';
  expect(router.currentRootComponent()).toBe(mockComponent);
  globals.state.route = null;
  expect(router.currentRootComponent()).toBe(mockComponent);
});

test('Returns component based on route in state', () => {
  globals.initialize({});
  const comp1 = {id: '1', view() {}};
  const comp2 = {id: '2', view() {}};
  const router = new Router('/1', {'/1': comp1, '/2': comp2});
  globals.state.route = '/1';
  expect(router.currentRootComponent()).toBe(comp1);
  globals.state.route = '/2';
  expect(router.currentRootComponent()).toBe(comp2);
});

test('Throws if state has invalid route', () => {
  globals.initialize({});
  const router = new Router('/1', {'/1': mockComponent});
  globals.state.route = '/2';
  expect(() => router.currentRootComponent()).toThrow();
});

test('Parse route from hash', () => {
  const router = new Router('/', {'/': mockComponent});
  window.location.hash = ROUTE_PREFIX + '/foobar?s=42';
  expect(router.getRouteFromHash()).toBe('/foobar');

  window.location.hash = '/foobar';  // Invalid prefix.
  expect(router.getRouteFromHash()).toBe('');
});

test('Cannot set invalid route', () => {
  const router = new Router('/', {'/': mockComponent});
  expect(() => router.setRouteOnHash('foo')).toThrow();
});

test('Set route on hash', () => {
  const router = new Router('/', {
    '/': mockComponent,
    '/a': mockComponent,
  });
  router.setRouteOnHash('/a');
  expect(window.location.hash).toBe(ROUTE_PREFIX + '/a');
});

test('Navigate on hash change', done => {
  const navigateAction = navigate('/viewer');
  const mockDispatch = (a: Action) => {
    expect(a).toEqual(navigateAction);
    done();
  };

  globals.initialize({
    dispatch: mockDispatch,
    router: new Router('/', {
      '/': mockComponent,
      '/viewer': mockComponent,
    })
  });
  globals.state.route = '/';
  window.location.hash = ROUTE_PREFIX + '/viewer';
});

test('Params parsing', () => {
  window.location.hash = '#!/foo?a=123&b=42&c=a?b?c';
  const router = new Router('/', {'/': mockComponent});
  expect(router.param('a')).toBe('123');
  expect(router.param('b')).toBe('42');
  expect(router.param('c')).toBe('a?b?c');
});
