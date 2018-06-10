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

console.log('Hello from the main thread!');
import { Adb } from './adb/adb';

function writeToUIConsole(line:string) {
  const lineElement = document.createElement('div');
  lineElement.innerText = line;
  const container = document.getElementById('console');
  if (!container)
    throw new Error('OMG');
  container.appendChild(lineElement);
}

// TODO(primiano): temporary for testing, just instantiates the WASM module on
// the main thread.
(<any>window).Module = {
    locateFile: (s: string) => '/wasm/' + s,
    print: writeToUIConsole,
    printErr: writeToUIConsole,
};

const g_adb = new Adb();
(<any>window).dev = new Adb();

function main() {
  const worker = new Worker("worker_bundle.js");
  worker.onerror = e => {
    console.error(e);
  }

  const adbCmd = <HTMLInputElement> document.getElementById('adb_cmd');
  const adbConnect = <HTMLButtonElement> document.getElementById('adb_connect');
  const adbOut = <HTMLDivElement> document.getElementById('adb_output');
  const adbPushFile = <HTMLInputElement> document.getElementById('adb_push_file');
  const adbPushTarget = <HTMLInputElement> document.getElementById('adb_push_target');
  adbConnect.addEventListener('click', () => {
    g_adb.connect();
    adbCmd.disabled = false;
  });
  const appendAdbLog = function(text:string) {
    const row = document.createElement('div');
    row.innerText = text;
    adbOut.appendChild(row);
    row.scrollIntoView();
  } 
  adbPushFile.addEventListener('change', () => {
    g_adb.sendFile(adbPushTarget.value, (<FileList> adbPushFile.files)[0]);
  });
  adbCmd.addEventListener('keypress', (k) => {
    if (k.keyCode != 13)
      return;
    appendAdbLog(adbCmd.value);
    g_adb.openStream(adbCmd.value)
    .then(stream => {
      stream.onData = (str, _) => { appendAdbLog(str); }
    });
  });

}

main();
