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
import httplib2
import json
import logging
import os
import socket
import select
import subprocess
import time


FIREBASE_DB = 'https://perfetto-ci.firebaseio.com'
JSON_HEADERS = {'Content-Type': 'application/json'}
DOCKER_CONTAINER = 'job'
DOCKER_IMAGE = 'worker'
LOGS_UPLOAD_SEC = 1
JOB_TIMEOUT_SEC = 10


def get_http():
  # credentials = AppAssertionCredentials(scope='https://www.googleapis.com/auth/compute')
  # http = credentials.authorize(httplib2.Http(memcache))
  return httplib2.Http()


def get_timestamp():
  return datetime.datetime.utcnow().strftime('%Y-%m-%d-%H-%M-%S')


def fb_get(path):
  uri = FIREBASE_DB + path
  http = get_http()
  resp, result = http.request(uri, headers={'X-Firebase-ETag': 'true'})
  if resp['status'] != '200':
    raise Exception(resp['status'], uri)
  return json.loads(result), resp['etag']


def fb_put(path, node, etag = None):
  uri = FIREBASE_DB + path
  body = json.dumps(node)
  headers = JSON_HEADERS.copy()
  if etag:
    headers['if_match'] = etag
  http = get_http()
  resp, _ = http.request(uri, headers=headers, method='PUT', body=body)
  if resp['status'] == '412':
    return False
  if resp['status'] != '200':
    raise Exception(resp['status'], uri)
  return True


def fb_patch(path, patch):
  uri = FIREBASE_DB + path
  body = json.dumps(patch)
  http = get_http()
  resp, _ = http.request(uri, headers=JSON_HEADERS, method='PATCH', body=body)
  if resp['status'] != '200':
    raise Exception(resp['status'], uri)


def fb_delete(path):
  uri = FIREBASE_DB + path
  http = get_http()
  resp, _ = http.request(uri, method='DELETE')
  if resp['status'] != '200':
    raise Exception(resp['status'], uri)


def get_vm_id():
  return socket.gethostname()

""" Takes ownership of a queued job.

Returns the job descriptor if successful, None otherwise.
"""
def try_take_job(job_name):
  uri = '/queued_jobs/%s.json' % job_name
  job, etag = fb_get(uri)
  if job and job.get('owner') and job.get('owner') != get_vm_id():
    return None
  job['owner'] = get_vm_id()
  if not fb_put(uri, job):
    return None
  fb_delete(uri)
  return job


def run_job(job_name, job):
  subprocess.call(['docker', 'rm', '-f', DOCKER_CONTAINER])
  cmd = ['docker', 'run', '--rm', '--name', DOCKER_CONTAINER]
  for k, v in job.iteritems():
    cmd += [ '-e', '%s=%s' % (k, v)]
  cmd += [ DOCKER_IMAGE ]
  logging.debug('Starting %s', ' '.join(cmd))
  with open(os.devnull) as devnull:
    proc = subprocess.Popen(cmd, stdin=devnull, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
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
    now = time.time()
    if now - last_upload >= LOGS_UPLOAD_SEC:
      last_upload = now
      if proc_stdout:
        fb_patch('/logs/%s/%s/stdout.json' % (job['cl'], job['ps']),
                 { get_timestamp(): '\n'.join(proc_stdout) })
      if proc_stderr:
        fb_patch('/logs/%s/%s/stderr.json' % (job['cl'], job['ps']),
                 { get_timestamp(): '\n'.join(proc_stderr) })
      proc_stdout = []
      proc_stderr = []
    if proc.poll() is not None:
      break
    if now - tstart > JOB_TIMEOUT_SEC:
      logging.warn('Job timed out after %d seconds. Killing', JOB_TIMEOUT_SEC)
      proc.kill()
      timeout = True
      break

  if timeout:
    status = 'timedout'
  elif proc.returncode == 0:
    status = 'succeded'
  else:
    status = 'failed'
  logging.info('Job %s %s after %d seconds', job_name, status,
      time.time() - tstart)
  fb_patch('/jobs/%s/%s.json' % (job['cl'], job['ps']),
           {'status': status, 'ts': get_timestamp()})


def main():
  logging.basicConfig(level=logging.DEBUG)
  while True:
    queue, _ = fb_get('/queued_jobs.json')
    if not queue:
      time.sleep(10)
      continue
    for job_name, job in queue.iteritems():
      logging.debug('Trying to acquire %s', job_name)
      job = try_take_job(job_name)
      if job is None:
        continue
      logging.info('Acquired job %s (%s/%s)', job_name, job['cl'], job['ps'])
      run_job(job_name, job)
      break


if __name__ == '__main__':
  main()
