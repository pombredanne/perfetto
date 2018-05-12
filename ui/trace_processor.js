'use strict';

// TODO: this should be written in typescript.

// Forwards calls to the worker.
class TraceProcessor {
  // |file| is a File object returned by HTML5 File API.
  constructor(file) {
    this.protobuf = new protobuf.Root();
    this.protobuf.resolvePath = (_, includePath) => '/protos/' + includePath;
    // this.pbRoot.load(['perfetto/processed_trace/sched.proto']);

    this.worker = new Worker('worker.js');
    this.worker.addEventListener('message', this.onWorkerMessage.bind(this));
    this.pendingRequests = {};
    this.lastReqId = 0;
  }

  // This function returns a JS object that matches perfectly the RPC service
  // described in //protos/perfetto/processed_trace/|name|.proto.
  // Example:
  // getInterface('sched').then(schedSvc => { schedSvc.GetSchedEvents(...); });
  getInterface(name) {
    return new Promise((resolve, reject) => {
      const path = 'perfetto/processed_trace/' + name + '.proto';
      this.protobuf.load(path).then(root => {
        try {
          const svc = root.lookupService('perfetto.protos.' + name);
          resolve([svc.create(this.rpcImpl.bind(this)), svc.parent]);
        } catch(err) {
          reject(err);
        }
      });
    });
  }

  rpcImpl(method, requestData, callback) {
    console.log(requestData);
  };

  // Sends a RPC call to the worker. Also handles bookkeeping for the reply.
  asyncCall(funcName, protoInput) {
    return new Promise((resolve, reject) => {
      const reqId = ++this.lastReqId;
      this.pendingRequests[reqId] = {resolve: resolve,
                                     reject: reject,
                                     funcName: funcName};
      this.worker.postMessage({reqId: reqId,
                               funcName: funcName,
                               input: protoInput.buffer,
                               inputLen: protoInput.length,
                             }/*,
                              [protoInput.buffer] /* transfer list */);
    });
  }

  // Invoked when the worker replies to our messages.
  onWorkerMessage(msg) {
    const reqId = msg.data.reqId;
    if (reqId in this.pendingRequests) {
      var pendingReq = this.pendingRequests[reqId];
      delete this.pendingRequests[reqId];
      pendingReq.resolve(msg.data.retValue);
    } else {
      console.log('ERROR: No pending reqId: ' + reqId);
    }
  }

}

var tp = new TraceProcessor('trace_name.protobuf');
