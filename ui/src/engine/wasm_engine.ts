import * as protobufjs from 'protobufjs/light';
import { TraceProcessor } from '../protos';
import { Engine } from './index';
import { WasmBridgeResponse, WasmBridgeRequest } from './wasm_bridge';

let gWarmWasmWorker: null|Worker = null;

function createWasmEngineWorker(): Worker {
  return new Worker("wasm_bundle.js");
}

function getWasmEngineWorker(): Worker {
  if (gWarmWasmWorker === null) {
    return createWasmEngineWorker();
  }
  const worker = gWarmWasmWorker;
  gWarmWasmWorker = createWasmEngineWorker();
  return worker;
}

/**
 * It's quite slow to compile WASM and (in Chrome) this happens every time
 * (there is no way to cache the compiled code currently). To mitigate this
 * we can always keep a WASM backend 'ready to go' just waiting to be provided
 * with a trace file. warmupWasmEngineWorker (together with getWasmEngineWorker)
 * implement this behaviour.
 */
export function warmupWasmEngineWorker(): void {
  if (gWarmWasmWorker !== null)
    return;
  gWarmWasmWorker = createWasmEngineWorker();
}

/**
 * This implementation of Engine uses a WASM backend hosted in a seprate worker
 * thread.
 */
export class WasmEngine extends Engine {
  private worker: Worker;
  private readonly traceProcessor_: TraceProcessor;
  private pendingCallbacks: Map<number, protobufjs.RPCImplCallback>;
  private nextRequestId: number;

  static create(blob: Blob): Engine {
    const worker = getWasmEngineWorker();
    worker.postMessage({
      blob,
    });
    return new WasmEngine(worker);
  }

  constructor(worker: Worker) {
    super();
    this.nextRequestId = 0;
    this.pendingCallbacks = new Map();
    this.worker = worker;
    this.worker.onerror = this.onError.bind(this);
    this.worker.onmessage = this.onMessage.bind(this);
    this.traceProcessor_ =
      TraceProcessor.create(this.rpcImpl.bind(this, 'trace_processor'));
  }

  get traceProcessor(): TraceProcessor {
    return this.traceProcessor_;
  }

  onError(e: ErrorEvent) {
    console.error(e);
  }

  onMessage(m: any) {
    const response = m.data as WasmBridgeResponse;
    const callback = this.pendingCallbacks.get(response.id);
    if (callback === undefined)
      throw `No such request: ${response.id}`;
    this.pendingCallbacks.delete(response.id);
    callback(null, response.data);
  }

  rpcImpl(
     serviceName: string,
     method: Function,
     requestData: Uint8Array,
     callback: protobufjs.RPCImplCallback): void {
    const methodName = method.name;
    const id = this.nextRequestId++;
    this.pendingCallbacks.set(id, callback);
    const request: WasmBridgeRequest = {
      id: id,
      serviceName,
      methodName,
      data: requestData,
    };
    this.worker.postMessage(request);
  }
}
