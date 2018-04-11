#!/bin/bash

for f in $(cat $1 | grep u:object_r:debugfs_tracing:s0 | grep tracing/events | awk '{ print $3 }' | xargs -n1 basename);do
  for x in $(find src/ftrace_reader/test/data/android_walleye_OPM5.171019.017.A1_4.4.88/events -wholename '*/'"$f"'/format' -or -wholename '*/'"$f"'/*/format'); do
    event=$(echo $x | awk -F / '{print $(NF - 1)}')
  n="protos/perfetto/trace/ftrace/$event.proto";
  if [ -f $n ]; then
    echo "'$n',"
    else
      echo "# MISSING $event"
      fi
    done
  done | sort
