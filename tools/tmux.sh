#!/bin/bash

set -e

outdir=out/arel

ninja -C $outdir perfetto traced traced_probes

set -x
adb push $outdir/traced /data/local/tmp/traced
adb push $outdir/traced_probes /data/local/tmp/traced_probes
adb push $outdir/perfetto /data/local/tmp/perfetto

set +x

if tmux has-session -t demo; then
  tmux kill-session -t demo
fi

tmux -2 new-session -d -s demo

if tmux -V | awk '{split($2, ver, "."); if (ver[1] < 2) exit 1 ; else if (ver[1] == 2 && ver[2] < 1) exit 1 }'; then
  tmux set-option -g mouse on
else
  tmux set-option -g mode-mouse on
  tmux set-option -g mouse-resize-pane on
  tmux set-option -g mouse-select-pane on
  tmux set-option -g mouse-select-window on
fi

tmux split-window -v
tmux split-window -v

tmux select-layout even-vertical

tmux select-pane -t 0
tmux send-keys "clear" C-m
tmux send-keys "adb shell" C-m

tmux select-pane -t 1
tmux send-keys "clear" C-m
tmux send-keys "adb shell" C-m

tmux select-pane -t 2
tmux send-keys "clear" C-m
tmux send-keys "adb shell" C-m

sleep 1

tmux select-pane -t 1
tmux send-keys "/data/local/tmp/traced" Enter

sleep 1

tmux select-pane -t 0
tmux send-keys "/data/local/tmp/traced_probes" Enter

tmux select-pane -t 2
tmux send-keys "/data/local/tmp/perfetto -c :test -o /data/local/tmp/trace"

# Select consumer pane.
tmux select-pane -t 1

tmux -2 attach-session -t demo

dst=$HOME/Downloads/trace.json
echo -e "\n\x1b[32mPulling trace into $dst\x1b[0m"
set -x
adb pull /data/local/tmp/trace /tmp/trace.protobuf
$outdir/trace_to_text systrace < /tmp/trace.protobuf > $dst
