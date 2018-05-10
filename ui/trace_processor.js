'use strict';

// TODO: this should be written in typescript.

// This will become the JS version of
// /protos/perfetto/processed_trace/query.proto.
var Query = undefined;
protobuf.load('/protos/perfetto/processed_trace/query.proto').then((root) => {
  Query = root.lookupType('perfetto.protos.Query');
  console.log('Protobuf types loaded');
});

// This class (or at least, its methods) will be auto-generated.
class TraceProcessor {
  // |file| is a File object returned by HTML5 File API.
  constructor(file) {
    this.worker = new Worker('worker.js');
    this.worker.addEventListener('message', this.onWorkerMessage.bind(this));
    this.pendingRequests = {};
    this.lastReqId = 0;
  }

  getSlices(query) {
    // (Aaaand this is why I hate JS so much).
    console.assert(query.__proto__ == Query.create().__proto__);

    var protobuf_encoded_query = Query.encode(query).finish();
    this.workerAsyncCall('GetSlices', protobuf_encoded_query)
        .then((x) => { console.log('getSlices reply: ', x); });
  }

// private:
  workerAsyncCall(funcName, protoInput) {
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

var p = new TraceProcessor('trace_name.protobuf');
