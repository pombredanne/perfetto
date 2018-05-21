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

// Messages sent from the main JS thread to the worker.
class MsgMainToWorker {
    wasmCall?: MethodCall;
    passedFile?: File;
}
class MethodCall {
    reqId: number = 0;
    svcName: string = '';
    funcName: string = '';
    inputLen: number = 0;
    inputOff: number = 0;
    input: ArrayBuffer = new ArrayBuffer(0);
}

// Messages sent from the worker back to the main JS thread.
class MsgWorkerToMain {
    wasmReply?: MethodReply;
    consoleOutput?: string
}
class MethodReply {
    reqId: number = 0;
    success: boolean = false;
    error?: string;
    data: ArrayBuffer = new ArrayBuffer(0);
}

class TraceProcessorWorker {
    constructor() { }

    onRuntimeInitialized() {
        console.log('WASM runtime ready');
        console.assert(!this.wasmReady);
        this.wasmReady = true;
        const readTraceFn = Module.addFunction(this.readTraceData.bind(this), 'iiii');
        const replyFn = Module.addFunction(this.reply.bind(this), 'viiii');

        Module.ccall('Initialize',
            'void',
            ['number', 'number'],
            [readTraceFn, replyFn]);

        // Process queued requests now that the module is ready.
        for (; ;) {
            const req = this.queuedCalls.shift();
            if (!req)
                break;
            this.callWasmMethod(req);
        }
    }

    readTraceData(offset: number, len: number, dstPtr: number) : number {
      if (!this.file) {
        console.assert(false);
        return 0;
      }
      const slice = this.file.slice(offset, offset + len);
      const buf:ArrayBuffer = this.fileReader.readAsArrayBuffer(slice);
      const buf8 = new Uint8Array(buf);
      Module.HEAPU8.set(buf8, dstPtr);
      return buf.byteLength;
    }

    reply(reqId: number, success: boolean, heapPtr: number, size: number) {
        const data = Module.HEAPU8.slice(heapPtr, heapPtr + size);
        var reply: MsgWorkerToMain = {
            wasmReply: {
                reqId: reqId,
                success: success,
                error: success ? undefined : (new TextDecoder()).decode(data),
                data: success ? data : undefined
            }
        };

        // TypeScript doesn't know that |self| is a WorkerGlobalScope and not
        // a Window. In WorkerGlobalScope, postMessage() takes only one
        // argument, as opposite to Window.
        (<any>self).postMessage(reply);
    }

    onMessage(msg: MessageEvent) {
        const req = <MsgMainToWorker>msg.data;
        if (req.wasmCall) {
            if (!this.wasmReady) {
                this.queuedCalls.push(req.wasmCall);
                return;
            }
            this.callWasmMethod(req.wasmCall);
        } else if (req.passedFile) {
          this.file = req.passedFile;
        }
    }

    onStdout(msg: string) {
        console.log(msg);
        var reply: MsgWorkerToMain = {
          consoleOutput: msg,
        };
        (<any>self).postMessage(reply);
    }

    callWasmMethod(req: MethodCall) {
        console.assert(this.wasmReady);
        var inputData = new Uint8Array(req.input, req.inputOff, req.inputOff + req.inputLen);
        Module.ccall(
            req.svcName + '_' + req.funcName,         // C function name.
            'void',                                   // Return type.
            ['number', 'array', 'number'],            // Input args.
            [req.reqId, inputData, inputData.length]  // Args.
        );
    }

    wasmReady: boolean = false;
    queuedCalls: Array<MethodCall> = [];
    file?: File;

    // @ts-ignore
    fileReader = new FileReaderSync();
}

// If we are in a worker context, initialize the worker and the WASM module.
// This file is pulled also from the main thread, just for the sake of getting
// the MsgMainToWorker/MsgWorkerToMain declarations.
if ((<any>self).WorkerGlobalScope !== undefined) {
    var tpw = new TraceProcessorWorker();

    // |Module| is a special var name introduced by the WASM compiler in the global
    // scope to interact with the corresponding c++ module.
    var Module: any = {
        locateFile: (s: string) => '/wasm/' + s,
        onRuntimeInitialized: tpw.onRuntimeInitialized.bind(tpw),
        print: tpw.onStdout.bind(tpw),
        printErr: tpw.onStdout.bind(tpw),
    };
    self.addEventListener('message', tpw.onMessage.bind(tpw));
    importScripts('wasm/trace_processor.js');
}
