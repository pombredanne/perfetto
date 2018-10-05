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

import collections
import webapp2
import json
import re
import sys
import httplib2
import logging
import time

from flask import Flask, abort, request
from google.appengine.api import urlfetch
from google.appengine.api import taskqueue
from google.appengine.api import memcache
from oauth2client.contrib.appengine import AppAssertionCredentials


GERRIT_ROOT = 'https://android.googlesource.com/platform/external/perfetto'
GIT_LS_REMOTE = GERRIT_ROOT + '/info/refs?service=git-upload-pack'
FIREBASE_DB = 'https://perfetto-ci.firebaseio.com/'
CI_CONFIGS = {}  # Lazily loaded from ci-configs.json
GCE_CONFIG = {}  # Lazily loaded from gce-vm-config.json
JSON_HEADERS = {'Content-Type': 'application/json'}
MAX_VMS = 2

VM_PREFIX = 'perfetto-ci-worker-'
INSTANCE_ZONE = 'us-central1-c'
PROJECT = 'perfetto-ci'


app = Flask(__name__)

@app.before_request
def enable_local_error_handling():
  app.logger.addHandler(logging.StreamHandler())
  app.logger.setLevel(logging.DEBUG)

def get_http():
  credentials = AppAssertionCredentials(scope='https://www.googleapis.com/auth/compute')
  http = credentials.authorize(httplib2.Http(memcache))
  return http


def get_configs():
  if not CI_CONFIGS:
    with open('ci-configs.json') as fd:
      CI_CONFIGS.update(json.load(fd))
  return CI_CONFIGS.items()


def get_gce_config():
  if not GCE_CONFIG:
    with open('gce-vm-config.json') as fd:
      GCE_CONFIG.update(json.load(fd))
  return GCE_CONFIG


def create_vm(suffix):
  cfg = get_gce_config().copy()
  cfg['name'] = VM_PREFIX + suffix
  logging.info('Creating VM %s', cfg['name'])
  http = get_http()
  uri = 'https://www.googleapis.com/compute/v1/projects/{project}/zones/{zone}/instances'
  uri = uri.format(project=PROJECT, zone=INSTANCE_ZONE)
  resp, result = http.request(uri=uri, method='POST', headers=JSON_HEADERS, body=json.dumps(cfg))
  print(resp)
  print '---------'
  print(result)
  print '---------'


def enqueue_cl(cl, patchset):
  for cfg_name, cfg_vars in get_configs():
    job_params = {
        'CFG': cfg_name,
        'REV': 'refs/changes/%s/%s/%s' % (cl[0:2], cl[2:], patchset)
    }
    job_params.update(cfg_vars)
    try:
      taskqueue.add(
          queue_name='jobs',
          name='%s-%s' % (cl, cfg_name),
          url='/',
          target='worker',
          params=job_params)
    except taskqueue.TaskAlreadyExistsError,_:
      continue

  # Mark as processed
  fetch = urlfetch.fetch(FIREBASE_DB + '/last_patchset.json',
                        method=urlfetch.PATCH,
                        payload=json.dumps({cl: patchset}),
                        headers=JSON_HEADERS)
  assert(fetch.status_code == 200)


# Returns a dict { 'CL NUMBER': LAST_PATCHSET_FOR_CL }
def get_processed_cls():
  fetch = urlfetch.fetch(FIREBASE_DB + '/last_patchset.json')
  db = json.loads(fetch.content)
  return db or {}


# Returns a dict { 'CL NUMBER': LAST_PATCHSET_FOR_CL }
def get_all_cls():
  last_patchset = collections.defaultdict(int)
  fetch = urlfetch.fetch(GIT_LS_REMOTE)
  for line in fetch.content.splitlines():
    # Git smart protocol line format:
    # [4 bytes line len in hex][40 bytes SHA1] refs/the/ref/name
    # Example:
    # 0046fd1489924674b3b833fd69748af4ba66cdab3cf7 refs/changes/39/560239/1
    # Note that the CL number is 39560239 ('39' + '560239').
    m = re.match(r'[0-9a-f]{4}([0-9a-f]{40})\s+refs/changes/(\d+)/(\d+)/(\d+).*', line)
    if m is None:
      continue
    sha1, cl1, cl2, patchset = m.groups()
    cl = cl1 + cl2
    try:
      patchset = int(patchset)
    except ValueError,e:
      continue
    last_patchset[cl] = max(last_patchset[cl], patchset)
  return dict(last_patchset)


def set_processed_patchsets(patchsets):
  patch = dict((x,{'processed': 1}) for x in patchsets)
  patch = json.dumps(patch)
  fetch = urlfetch.fetch(FIREBASE_DB + '.json',
                        method=urlfetch.PATCH,
                        payload=patch,
                        headers=JSON_HEADERS)
  # print >>sys.stderr, fetch.content


def get_all_vms():
  http = get_http()
  uri = 'https://www.googleapis.com/compute/v1/projects/{project}/zones/{zone}/instances'
  uri = uri.format(project=PROJECT, zone=INSTANCE_ZONE)
  resp, result = http.request(uri=uri, method='GET')
  if resp['status'] != '200':
    raise Exception(resp['status'], uri)
  vms = json.loads(result)
  if not vms.get('items'):
    return []
  return {x['name']: x['status'] for x in vms['items'] if x['name'].startswith(VM_PREFIX)}


@app.route('/poller/destroy_vms')
def destroy_vms():
  logging.info('Destroying all vms')
  # First destroy all vms
  vms = get_all_vms().keys()
  for vm in vms:
    logging.info('Destroying %s', vm)
    http = get_http()
    uri = 'https://www.googleapis.com/compute/v1/projects/{project}/zones/{zone}/instances/{vm}'
    uri = uri.format(project=PROJECT, zone=INSTANCE_ZONE, vm=vm)
    resp, result = http.request(uri=uri, method='DELETE')
    if resp['status'] != '200':
      raise Exception(resp['status'], uri)
  return ' '.join(vms)

@app.route('/poller/recreate_vms')
def recreate_vms():
  destroy_vms()
  now = int(time.time())
  for i in xrange(MAX_VMS):
    create_vm('%d-%d' % (now, i))
  return 'Done'


@app.route('/poller/')
def list_vms():
  # http = get_http()
  # uri = 'https://www.googleapis.com/compute/v1/projects/{project}/zones/{zone}/instances'
  # uri = uri.format(project=PROJECT, zone=INSTANCE_ZONE, instance=INSTANCE_NAME)
  # resp, result = http.request(uri=uri, method='GET')
  # print(resp)
  # print(json.loads(result)['items'])
  return '%s' % get_all_vms()
  # self.response.write(result)
  # all_patchsets = get_all_cls()
  # processed = get_processed_cls()
  # pending = {}
  # for cl, last_patchset in all_patchsets.iteritems():
  #   if cl not in processed or processed[cl] < last_patchset:
  #     pending[cl] = last_patchset
  #     continue
  # for cl, patchset in pending.iteritems():
  #   enqueue_cl(cl, patchset)
  #   self.response.write('%s - %s\n' % (cl, patchset))


@app.route('/poller/mark_all_done')
def mark_all_done():
  all_patchsets = json.dumps(get_all_cls())
  fetch = urlfetch.fetch(FIREBASE_DB + '/last_patchset.json',
                        method=urlfetch.PUT,
                        payload=all_patchsets,
                        headers=JSON_HEADERS)
  return fetch.content, fetch.status_code, JSON_HEADERS
