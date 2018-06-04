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

import { State, createZeroState } from '../backend/state';
import * as m from 'mithril';

let gState: State = createZeroState();
let gDispatch: (msg: any) => void = _ => {};

function q(f: (e: any) => void): (e: any) => void {
  return function(e) {
    e.redraw = false;
    f(e);
  };
}

function incrementCounter() {
  return {
    topic: 'inc',
  };
}

function navigate(fragment: string) {
  return {
    topic: 'navigate',
    fragment,
  };
}

function checkBox(checked: boolean) {
  return {
    topic: 'check',
    checked,
  };
}

const Menu: m.Component<{ title: string }> = {
  view(vnode) {
    return m("#menu",
      m('h1', vnode.attrs.title),
    );
  },
};

const Side = {
  view: function() {
    return m("#side",
      m('#masthead',
        m("img#logo[src='logo.png'][width=384px][height=384px]"),
        m("h1", "Perfetto"),
      ),
      m('ul.items', 
        m('li', { onclick: q(_ => gDispatch(navigate('/config'))) }, 'Config'),
        m('li', { onclick: q(_ => gDispatch(navigate('/control'))) }, 'Control'),
      ),
    );
  },
};

const ControlPage = {
  view: function() {
    return [
      m(Menu, { title: "Control" }),
      m(Side),
      m('#content', 
        m("h1", {class: "title"}, `Counter: ${gState.counter}`),
        m("button", {
          onclick: q(_ => gDispatch(incrementCounter())),
        }, "A button"),
      ),
    ];
  },
};

const ConfigPage = {
  view: function() {
    return [
      m(Menu, { title: "Config Editor" }),
      m(Side),
      m('#content', 
        m('label', m('input[type=checkbox]', {
          checked: gState.checked,
          onchange: q(m.withAttr('checked', c => gDispatch(checkBox(c)))),
        }), 'sched'),
        m("button", {
          onclick: q(_ => gDispatch(incrementCounter())),
        }, "A button"),
      ),
    ];
  },
};

function readState(): State {
  const checked: boolean = !!m.route.param('checked');
  const state: State = createZeroState();
  state.checked = checked;
  return state;
}

function updateState(new_state: State): void {
  const old_state = gState;
  gState = new_state;

  if (old_state.fragment == new_state.fragment) {
    m.redraw();
    return;
  }
  m.route.set(gState.fragment, {}, {
    replace: false,
    state: {
      checked: gState.checked,
    }
  });
}

function main() {
  console.log('Hello from the main thread!');
  const worker = new Worker("worker_bundle.js");
  worker.onerror = e => {
    console.error(e);
  }
  worker.onmessage = msg => {
    switch (msg.data.topic) {
      case 'pong':
        console.log('Worker ACKed.');
        break;
      case 'new_state':
        updateState(msg.data.new_state);
        break;
    }
  };
  worker.postMessage({ topic: 'ping' });

  const root = document.querySelector('main');
  if (root == null) {
    console.error('No main element found.');
    return;
  }
  m.route(root, "/control", {
    "/control": ControlPage,
    "/config": ConfigPage,
  });

  gState = readState();
  m.redraw();

  gDispatch = worker.postMessage.bind(worker);
  console.log('initial:', gState);
  gDispatch({
    topic: 'init',
    initial_state: gState,
  });
}

export {
  main,
};
