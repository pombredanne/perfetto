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

import * as protobuf from "protobufjs";

// TODO(primiano): I'm pretty sure there is a saner way to express this.
import * as protos_ns from './gen/protos';
import protos = protos_ns.perfetto.protos;

import './worker'  // Only for the WASMRequst/WASMReply declarations.

// This class is a shim intended to be used on the main thread. It routes method
// calls to the worker (where the WASM module lives) and handles all the async
// dispatching, wrapping those methods into promises.
export default class TraceProcessor {
    constructor(traceFile: Blob, console: HTMLElement) {
        this.console = console;
        this.traceFile = traceFile;
        this.worker = new Worker('worker.js');
        this.worker.addEventListener('message', this.onWorkerMessage.bind(this));
    }

    // Returns the Sched RPC interface defined in sched.proto.
    sched: protos.Sched = new protos.Sched(this.rpcImpl.bind(this, 'sched'));

    rpcImpl(svcName: string,
        method: Function,
        requestData: Uint8Array,
        callback: protobuf.RPCImplCallback) {
        this.callWASM(svcName, method.name, requestData)
            .then((res: Uint8Array) => {
                console.assert(res && res.length > 0);
                callback(/*error=*/null, res);
            })
            .catch(err => { callback(new Error(err)); });
    }

    callWASM(svcName: string, funcName: string, protoInput: Uint8Array) {
        return new Promise<Uint8Array>((resolve, reject) => {
            const reqId = ++this.lastReqId;
            this.pendingRequests[reqId] = { resolve: resolve, reject: reject };
            this.worker.postMessage(<WASMRequest>{
                wasmCall: {
                    reqId: reqId,
                    svcName: svcName,
                    funcName: funcName,
                    input: protoInput.buffer,
                    inputLen: protoInput.length,
                }
            }/*,
                [protoInput.buffer] /* transfer list */);
        });
    }

    onWorkerMessage(msg: MessageEvent) {
        const msgData = <WASMReply>msg.data;
        if (msgData.wasmReply) {
            const reply = msgData.wasmReply;
            if (!(reply.reqId in this.pendingRequests)) {
                console.error('ERROR: No pending request with ID:', reply.reqId);
                return;
            }
            const pendingReq = this.pendingRequests[reply.reqId];
            delete this.pendingRequests[reply.reqId];
            if (reply.success) {
                pendingReq.resolve(reply.data);
            } else {
                pendingReq.reject(new Error(reply.error));
            }
        } else if (msgData.readData) {
            const readData = msgData.readData;
            var fr = new FileReader();
            fr.addEventListener('load', () => {
                var buf: ArrayBuffer = fr.result;
                this.worker.postMessage(<WASMRequest>{
                  readData: {
                    offset: readData.offset,
                    data: buf
                  }
                }, [buf] /* transfer list */);

            });
            fr.readAsArrayBuffer(this.traceFile.slice(
                readData.offset, readData.offset + readData.len));
        } else if (msgData.consoleOutput) {
          var line = document.createElement('div');
          const t = ((Date.now() - this.firstConsoleMsgTime) / 1000).toFixed(3);
          line.innerText = '[' + t + '] ' + msgData.consoleOutput;
          this.console.appendChild(line);
        }
    }

    console: HTMLElement;
    firstConsoleMsgTime: number = Date.now();
    worker: Worker;
    traceFile: Blob;
    pendingRequests: { [key: number]: PendingPromise } = {};
    lastReqId: number = 0;
}

class PendingPromise {
    resolve: any;
    reject: any;
}
