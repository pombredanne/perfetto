'use strict';

// Shameless copy-paste from the other .js.
// self.importScripts('//cdn.rawgit.com/dcodeIO/protobuf.js/6.8.6/dist/protobuf.js');
// var Query = undefined;
// protobuf.load('/protos/perfetto/processed_trace/query.proto').then((root) => {
//   Query = root.lookupType('perfetto.protos.Query');
//   console.log('[worker] Protobuf types loaded');
// });

var Module = { locateFile: (x) => '/out/mac_debug/wasm/' + x };
importScripts('/out/mac_debug/wasm/wasm_js.js');


self.onmessage = function (msg) {
  console.log(msg);
  var inputData = new Uint8Array(msg.data.input, 0, msg.data.inputLen);
  Module.ccall(
    msg.data.funcName,             // C function name.
    'void',                        // return type.
    ['array', 'number'],           // args type (const char* buf, size_t len).
    [inputData, inputData.length]  // args.
  );


  // var query = Query.decode(arr);
  // console.log(query);
  var reply = {
    reqId: msg.data.reqId,
    retValue: 'The return value for ' + msg.data.funcName,
  };
  setTimeout(() => { self.postMessage(reply); }, 0);
}
