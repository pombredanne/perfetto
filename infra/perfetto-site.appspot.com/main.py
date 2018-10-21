# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from google.appengine.api import memcache
from google.appengine.api import urlfetch

import webapp2

BASE = 'https://catapult-project.github.io/perfetto/%s'

class MainHandler(webapp2.RequestHandler):
    def get(self):
        handler = GithubMirrorHandler()
        handler.initialize(self.request, self.response)
        return handler.get("index.html")


class GithubMirrorHandler(webapp2.RequestHandler):
    def get(self, resource):
        if '..' in resource:
          self.response.set_status(403)
          return

        url = BASE % resource
        contents = memcache.get(url)
        if not contents or self.request.get('reload'):
            result = urlfetch.fetch(url)
            if result.status_code != 200:
                self.response.set_status(result.status_code)
                self.response.write(result.content)
                return
            contents = result.content
            memcache.set(url, contents, time=60*60*24)  # 1 day

        self.response.headers['Content-Type'] = result.headers['Content-Type']
        self.response.write(contents)


app = webapp2.WSGIApplication([
    ('/', MainHandler),
    ('/(.+)', GithubMirrorHandler),
], debug=True)
