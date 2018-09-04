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

import {navigate} from '../common/actions';

import {globals} from './globals';

interface RouteMap {
  [route: string]: m.Component;
}

export const ROUTE_PREFIX = '#!';

export class Router {
  constructor(private defaultRoute: string, private routes: RouteMap) {
    if (!(defaultRoute in routes)) {
      throw Error('routes must define a component for defaultRoute.');
    }

    window.onhashchange = () => this.onhashchange();
  }

  getRouteFromHash(): string {
    const prefixLength = ROUTE_PREFIX.length;
    const hash = window.location.hash;

    // Do not try to parse route if prefix doesn't match.
    if (hash.substring(0, prefixLength) !== ROUTE_PREFIX) return '';

    return hash.split('?')[0].substring(prefixLength);
  }

  setRouteOnHash(route: string) {
    if (!(route in this.routes)) throw Error('Invalid route.');
    window.location.hash = ROUTE_PREFIX + route;
  }

  currentRootComponent(): m.Component {
    const route = globals.state.route || this.defaultRoute;
    if (!(route in this.routes)) throw Error('State has invalid route');
    return this.routes[globals.state.route || this.defaultRoute];
  }

  param(key: string) {
    const hash = window.location.hash;
    const paramStart = hash.indexOf('?');
    if (paramStart === -1) return undefined;
    return m.parseQueryString(hash.substring(paramStart))[key];
  }

  private onhashchange() {
    const routeOnHash = this.getRouteFromHash();
    const nextRoute =
        routeOnHash in this.routes ? routeOnHash : this.defaultRoute;
    if (globals.state.route !== nextRoute) {
      // The route hash was changed before the state was updated, possibly
      // through manual user update of the URL. Inform the controller.
      globals.dispatch(navigate(nextRoute));
    }
    // TODO: Handle case when new route has a new permalink.
  }
}
