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

// TODO(hjd): Move to own npm module.
const handler = {
  get(target, prop, receiver) {
    if (prop === '__name') return target.__name;
    if (prop === 'inspect') return target.inspect;
    if (prop === '__accessor') return target.__accessor;
    if (prop === 'calls') return target.calls;
    if (prop === 'toString') return () => `<Dingus(${target.__name})>`;
    if (typeof prop === 'symbol') return target[prop];
    if (target[prop] === undefined)
      target[prop] = dingus(`${target.__name}.${prop}`, receiver, prop);
    return target[prop];
  },

  apply(target, thisArg, args) {
    if (!target.__result) target.__result = dingus(`${target.__name}()`);
    if (target.__parent && target.__accessor)
      target.__parent.calls.push(
          [`${target.__accessor}()`, args, target.__result]);
    target.calls.push(['()', args, target.__result]);
    return target.__result;
  },
};

/**
 * Type safe record-then-assert test double in the vein of
 * https://github.com/garybernhardt/dingus
 */
function dingus(opt_name, opt_parent, opt_accessor) {
  const f = () => {};
  f.calls = [];
  f.__name = opt_name === undefined ? '' : opt_name;
  f.__parent = opt_parent;
  f.__accessor = opt_accessor;
  return new Proxy(f, handler)
}

module.exports = {
  dingus,
};
