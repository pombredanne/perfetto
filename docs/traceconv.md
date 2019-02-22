# traceconv

traceconv is the de-facto tool used to convert Perfetto traces into formats
used by other tools.

The formats supported today are as follows:
 * proto text format: the stanard text based representation of protos
 * Chrome JSON format: the format used by chrome://tracing
 * systrace format: the format used by the Android systrace and tools from this ecosystem
 * profile format (heap profiler only): the format used by heap dumps - this is
   only valid if there are heap profiles in the trace

traceconv is also used in the UI to convert Perfetto traces to the Chrome
JSON format and directly open these traces in chrome://tracing.

Usage
---------
```
curl https://get.perfetto.dev/traceconv -o traceconv
chmod +x tracecov
./traceconv [text|json|systrace|profile] [intput proto file] [output file]
```

Examples
---------

### Converting a perfetto trace to systrace text format
`./traceconv systrace [input proto file] [output systrace file]`

### Opening a Perfetto trace in chrome://tracing
Navigate to ui.perfetto.dev and choose the "Open with legacy UI" option. This
runs traceconv (the progress of which can be seen in the UI) and passes the
converted trace seamlessly to chrome://tracing

### Converting a perfetto trace to Chrome JSON format (can be loaded in chrome://tracing)
`./traceconv json [input proto file] [output json file]`
