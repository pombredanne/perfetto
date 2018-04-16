mergeInto(LibraryManager.library, {
  FetchTrace: function(offset, length) {
    FetchTrace(offset, length);
  },
  TraceStatusUpdate: function(bytes_loaded, complete, duration) {
    TraceStatusUpdate(bytes_loaded, complete, duration);
  },
});
