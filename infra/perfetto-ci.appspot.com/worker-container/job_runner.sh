#!/bin/bash

env

rm -rf /home/bot/repo
mkdir -p /home/bot/repo
cd /home/bot/repo

if [ "$REV" == "" ]; then
  echo "REV env var is empty, expecting something like refs/changes/12/3456/78"
  exit 1
fi

set -e

git init
git remote add origin https://android.googlesource.com/platform/external/perfetto
git fetch "$REV"
exec test/ci-runner.sh
