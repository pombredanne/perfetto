#!/bin/bash

for f in $(cat $1 | grep u:object_r:debugfs_tracing:s0 | grep tracing/events | awk '{ print $3 }' | xargs -n1 basename);do
  if [ -f "protos/perfetto/trace/ftrace/$f.proto" ]; then
    echo "'protos/perfetto/trace/ftrace/$f.proto',";
  else
    for x in $(find src/ftrace_reader/test/data/*/events -wholename '*/'"$f"'/format' -or -wholename '*/'"$f"'/*/format'); do
      event=$(echo $x | awk -F / '{print $(NF - 1)}')
      n="protos/perfetto/trace/ftrace/$event.proto";
      if [ -f $n ]; then
        echo "'$n',"
      else
          echo "# MISSING $event"
      fi
    done
  fi
done | sort
