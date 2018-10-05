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

import datetime
import os
import json
import select
import subprocess
import time
import urllib2

from cgi import parse_qs
from wsgiref.simple_server import make_server


FIREBASE_DB = 'https://perfetto-ci.firebaseio.com'
JSON_HEADERS = {'Content-Type': 'application/json'}
JOB_RUNNER = os.path.join(os.path.dirname(__file__), 'job_runner.sh')
LOGS_UPLOAD_SEC = 1
JOB_TIMEOUT_SEC = 60 * 30

def get_timestamp():
  return datetime.datetime.utcnow().strftime('%Y-%m-%d-%H-%M-%S')


def update_job_status(args, status):
  print status
  url = FIREBASE_DB + '/jobs/%s/%s.json' % (args['CL'], args['PATCHSET'])
  data = json.dumps({'status': status, 'ts': get_timestamp() })
  req = urllib2.Request(url=url, data=data, headers=JSON_HEADERS)
  req.get_method = lambda: 'PUT'
  urllib2.urlopen(req)


def delete_logs(args):
  url = FIREBASE_DB + '/logs/%s/%s.json' % (args['CL'], args['PATCHSET'])
  req = urllib2.Request(url=url, data={}, headers=JSON_HEADERS)
  req.get_method = lambda: 'DELETE'
  urllib2.urlopen(req)


def update_logs(args, proc_stdout, proc_stderr):
  for k, v in [('stdout', proc_stdout), ('stderr', proc_stderr)]:
    if not v:
      continue
    data = json.dumps({get_timestamp(): '\n'.join(v)})
    url = FIREBASE_DB + '/logs/%s/%s/%s.json' % (args['CL'], args['PATCHSET'], k)
    req = urllib2.Request(url=url, data=data, headers=JSON_HEADERS)
    req.get_method = lambda: 'PATCH'
    urllib2.urlopen(req)


def simple_app(environ, start_response):
    HEADERS = [('Content-Type', 'text/plain')]

    if environ['PATH_INFO'] == '/_ah/health':
      start_response('200 OK', HEADERS)
      return 'healty'

    args = {}
    req_valid = False
    if environ['REQUEST_METHOD'] == 'POST':
        request_body_size = int(environ.get('CONTENT_LENGTH', 0))
        request_body = environ['wsgi.input'].read(request_body_size)
        args = parse_qs(request_body)
        for k, values in args.iteritems():
          args[k] = values[0] if values else ''
        req_valid = 'REV' in args

    if not req_valid:
      start_response('500 Internal Server Error', HEADERS)
      return 'Invalid request (%s)' % args

    start_response('200 OK', HEADERS)
    update_job_status(args, 'started')
    delete_logs(args)
    env = {}
    env.update(os.environ)
    env.update(args)
    with open(os.devnull) as devnull:
      proc = subprocess.Popen(['bash', JOB_RUNNER], env=env, stdin=devnull,
          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    proc_stdout = []
    proc_stderr = []
    last_upload = 0
    tstart = time.time()
    timeout = False
    while True:
      fds = [proc.stdout.fileno(), proc.stderr.fileno()]
      sel = select.select(fds, [], [], 1)
      for fd in sel[0]:
        if fd == proc.stdout.fileno():
          proc_stdout.append(proc.stdout.readline())
        if fd == proc.stderr.fileno():
          proc_stderr.append(proc.stderr.readline())
      if time.time() - last_upload > LOGS_UPLOAD_SEC:
        update_logs(args, proc_stdout, proc_stderr)
        proc_stdout = []
        proc_stderr = []
      if proc.poll() is not None:
        break
      if time.time() - tstart > JOB_TIMEOUT_SEC:
        proc.kill()
        timeout = True
        break

    if timeout:
      status = 'timedout'
    elif proc.returncode == 0:
      status = 'succeded'
    else:
      status = 'failed'

    update_job_status(args, status)
    return status


if __name__ == '__main__':
  print 'Starting worker...'
  server = make_server('', 8080, simple_app)
  server.serve_forever()
