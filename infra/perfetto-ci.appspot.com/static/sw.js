var CACHE_NAME = 'travis-cache';
self.addEventListener('fetch', function(event) {
  var response = caches.match(event.request)
    .then(function(response) {
      // Cache hit - return response
      if (response) {
        return response;
      }

      // Clone the request to access it later.
      var fetchRequest = event.request.clone();
      return fetch(event.request);
    })
    .then(function(response) {
      // Check if we received a valid response
      if (!response || response.status !== 200) {
        return response;
      }

      // Check if the response we got should be cached.
      return response.clone().json().then(function(json) {
        let jobState = json.state;
        if (json.state == 'finished' && json.result !== 0)
          jobState = 'errored';

        if (jobState == 'errored' || jobState == 'cancelled' 
            || jobState == 'finished')
          return response

        var response = responseToCache.clone();
        caches.open(CACHE_NAME)
          .then(function(cache) {
            cache.put(event.request, responseToCache);
          });

        return response;
      });
    });
  event.respondWith(response);
});
