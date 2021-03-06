# Copyright (C) 2019 The Android Open Source Project
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
#
# This file is automatically generated by tools/gen_build. Do not edit.

load("//tools/build_defs/proto/cpp:cc_proto_library.bzl", "cc_proto_library")
load("//third_party/perfetto/google:build_defs.bzl", "pbzero_cc_proto_library")

package(default_visibility = ["//third_party/perfetto:__subpackages__"])

licenses(["notice"])  # Apache 2.0

exports_files(["LICENSE"])

# GN target: //protos/perfetto/trace/android:lite_gen
proto_library(
    name = "android",
    srcs = [
        "perfetto/trace/android/android_log.proto",
        "perfetto/trace/android/packages_list.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:common",
    ],
)

# GN target: //protos/perfetto/trace/android:lite_gen
cc_proto_library(
    name = "android_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:android",
    ],
)

# GN target: //protos/perfetto/trace/android:zero_gen
proto_library(
    name = "android_zero",
    srcs = [
        "perfetto/trace/android/android_log.proto",
        "perfetto/trace/android/packages_list.proto",
    ],
    deps = [
        "//third_party/perfetto/protos:common_zero",
    ],
)

# GN target: //protos/perfetto/trace/android:zero_gen
pbzero_cc_proto_library(
    name = "android_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:android_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/trace/chrome:lite_gen
proto_library(
    name = "chrome",
    srcs = [
        "perfetto/trace/chrome/chrome_trace_event.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/trace/chrome:lite_gen
cc_proto_library(
    name = "chrome_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:chrome",
    ],
)

# GN target: //protos/perfetto/trace/chrome:zero_gen
proto_library(
    name = "chrome_zero",
    srcs = [
        "perfetto/trace/chrome/chrome_trace_event.proto",
    ],
    deps = [
        "//third_party/perfetto/protos:interned_data_zero",
        "//third_party/perfetto/protos:track_event_zero",
    ],
)

# GN target: //protos/perfetto/trace/chrome:zero_gen
pbzero_cc_proto_library(
    name = "chrome_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:chrome_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/common:lite_gen
proto_library(
    name = "common",
    srcs = [
        "perfetto/common/android_log_constants.proto",
        "perfetto/common/commit_data_request.proto",
        "perfetto/common/observable_events.proto",
        "perfetto/common/sys_stats_counters.proto",
        "perfetto/common/trace_stats.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/common:lite_gen
cc_proto_library(
    name = "common_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:common",
    ],
)

# GN target: //protos/perfetto/common:zero_gen
proto_library(
    name = "common_zero",
    srcs = [
        "perfetto/common/android_log_constants.proto",
        "perfetto/common/commit_data_request.proto",
        "perfetto/common/observable_events.proto",
        "perfetto/common/sys_stats_counters.proto",
        "perfetto/common/trace_stats.proto",
    ],
)

# GN target: //protos/perfetto/common:zero_gen
pbzero_cc_proto_library(
    name = "common_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:common_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/config:lite_gen
proto_library(
    name = "config",
    srcs = [
        "perfetto/config/android/android_log_config.proto",
        "perfetto/config/chrome/chrome_config.proto",
        "perfetto/config/data_source_config.proto",
        "perfetto/config/data_source_descriptor.proto",
        "perfetto/config/ftrace/ftrace_config.proto",
        "perfetto/config/inode_file/inode_file_config.proto",
        "perfetto/config/power/android_power_config.proto",
        "perfetto/config/process_stats/process_stats_config.proto",
        "perfetto/config/profiling/heapprofd_config.proto",
        "perfetto/config/sys_stats/sys_stats_config.proto",
        "perfetto/config/test_config.proto",
        "perfetto/config/trace_config.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:common",
    ],
)

# GN target: //protos/perfetto/config:lite_gen
cc_proto_library(
    name = "config_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:config",
    ],
)

# GN target: //protos/perfetto/config:zero_gen
proto_library(
    name = "config_zero",
    srcs = [
        "perfetto/config/android/android_log_config.proto",
        "perfetto/config/chrome/chrome_config.proto",
        "perfetto/config/data_source_config.proto",
        "perfetto/config/data_source_descriptor.proto",
        "perfetto/config/ftrace/ftrace_config.proto",
        "perfetto/config/inode_file/inode_file_config.proto",
        "perfetto/config/power/android_power_config.proto",
        "perfetto/config/process_stats/process_stats_config.proto",
        "perfetto/config/profiling/heapprofd_config.proto",
        "perfetto/config/sys_stats/sys_stats_config.proto",
        "perfetto/config/test_config.proto",
        "perfetto/config/trace_config.proto",
    ],
    deps = [
        "//third_party/perfetto/protos:common_zero",
    ],
)

# GN target: //protos/perfetto/config:zero_gen
pbzero_cc_proto_library(
    name = "config_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:config_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/trace/filesystem:lite_gen
proto_library(
    name = "filesystem",
    srcs = [
        "perfetto/trace/filesystem/inode_file_map.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/trace/filesystem:lite_gen
cc_proto_library(
    name = "filesystem_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:filesystem",
    ],
)

# GN target: //protos/perfetto/trace/filesystem:zero_gen
proto_library(
    name = "filesystem_zero",
    srcs = [
        "perfetto/trace/filesystem/inode_file_map.proto",
    ],
)

# GN target: //protos/perfetto/trace/filesystem:zero_gen
pbzero_cc_proto_library(
    name = "filesystem_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:filesystem_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/trace/ftrace:lite_gen
proto_library(
    name = "ftrace",
    srcs = [
        "perfetto/trace/ftrace/binder.proto",
        "perfetto/trace/ftrace/block.proto",
        "perfetto/trace/ftrace/cgroup.proto",
        "perfetto/trace/ftrace/clk.proto",
        "perfetto/trace/ftrace/compaction.proto",
        "perfetto/trace/ftrace/ext4.proto",
        "perfetto/trace/ftrace/f2fs.proto",
        "perfetto/trace/ftrace/fence.proto",
        "perfetto/trace/ftrace/filemap.proto",
        "perfetto/trace/ftrace/ftrace.proto",
        "perfetto/trace/ftrace/ftrace_event.proto",
        "perfetto/trace/ftrace/ftrace_event_bundle.proto",
        "perfetto/trace/ftrace/ftrace_stats.proto",
        "perfetto/trace/ftrace/generic.proto",
        "perfetto/trace/ftrace/i2c.proto",
        "perfetto/trace/ftrace/ipi.proto",
        "perfetto/trace/ftrace/irq.proto",
        "perfetto/trace/ftrace/kmem.proto",
        "perfetto/trace/ftrace/lowmemorykiller.proto",
        "perfetto/trace/ftrace/mdss.proto",
        "perfetto/trace/ftrace/mm_event.proto",
        "perfetto/trace/ftrace/oom.proto",
        "perfetto/trace/ftrace/power.proto",
        "perfetto/trace/ftrace/raw_syscalls.proto",
        "perfetto/trace/ftrace/regulator.proto",
        "perfetto/trace/ftrace/sched.proto",
        "perfetto/trace/ftrace/signal.proto",
        "perfetto/trace/ftrace/sync.proto",
        "perfetto/trace/ftrace/task.proto",
        "perfetto/trace/ftrace/test_bundle_wrapper.proto",
        "perfetto/trace/ftrace/vmscan.proto",
        "perfetto/trace/ftrace/workqueue.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/trace/ftrace:lite_gen
cc_proto_library(
    name = "ftrace_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:ftrace",
    ],
)

# GN target: //protos/perfetto/trace/ftrace:zero_gen
proto_library(
    name = "ftrace_zero",
    srcs = [
        "perfetto/trace/ftrace/binder.proto",
        "perfetto/trace/ftrace/block.proto",
        "perfetto/trace/ftrace/cgroup.proto",
        "perfetto/trace/ftrace/clk.proto",
        "perfetto/trace/ftrace/compaction.proto",
        "perfetto/trace/ftrace/ext4.proto",
        "perfetto/trace/ftrace/f2fs.proto",
        "perfetto/trace/ftrace/fence.proto",
        "perfetto/trace/ftrace/filemap.proto",
        "perfetto/trace/ftrace/ftrace.proto",
        "perfetto/trace/ftrace/ftrace_event.proto",
        "perfetto/trace/ftrace/ftrace_event_bundle.proto",
        "perfetto/trace/ftrace/ftrace_stats.proto",
        "perfetto/trace/ftrace/generic.proto",
        "perfetto/trace/ftrace/i2c.proto",
        "perfetto/trace/ftrace/ipi.proto",
        "perfetto/trace/ftrace/irq.proto",
        "perfetto/trace/ftrace/kmem.proto",
        "perfetto/trace/ftrace/lowmemorykiller.proto",
        "perfetto/trace/ftrace/mdss.proto",
        "perfetto/trace/ftrace/mm_event.proto",
        "perfetto/trace/ftrace/oom.proto",
        "perfetto/trace/ftrace/power.proto",
        "perfetto/trace/ftrace/raw_syscalls.proto",
        "perfetto/trace/ftrace/regulator.proto",
        "perfetto/trace/ftrace/sched.proto",
        "perfetto/trace/ftrace/signal.proto",
        "perfetto/trace/ftrace/sync.proto",
        "perfetto/trace/ftrace/task.proto",
        "perfetto/trace/ftrace/test_bundle_wrapper.proto",
        "perfetto/trace/ftrace/vmscan.proto",
        "perfetto/trace/ftrace/workqueue.proto",
    ],
)

# GN target: //protos/perfetto/trace/ftrace:zero_gen
pbzero_cc_proto_library(
    name = "ftrace_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:ftrace_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/trace/interned_data:lite_gen
proto_library(
    name = "interned_data",
    srcs = [
        "perfetto/trace/interned_data/interned_data.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:track_event",
    ],
)

# GN target: //protos/perfetto/trace/interned_data:lite_gen
cc_proto_library(
    name = "interned_data_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:interned_data",
    ],
)

# GN target: //protos/perfetto/trace/interned_data:zero_gen
proto_library(
    name = "interned_data_zero",
    srcs = [
        "perfetto/trace/interned_data/interned_data.proto",
    ],
    deps = [
        "//third_party/perfetto/protos:track_event_zero",
    ],
)

# GN target: //protos/perfetto/trace/interned_data:zero_gen
pbzero_cc_proto_library(
    name = "interned_data_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:interned_data_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/metrics:lite_gen
proto_library(
    name = "metrics",
    srcs = [
        "perfetto/metrics/metrics.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/metrics:lite_gen
cc_proto_library(
    name = "metrics_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:metrics",
    ],
)

# GN target: //protos/perfetto/trace/power:lite_gen
proto_library(
    name = "power",
    srcs = [
        "perfetto/trace/power/battery_counters.proto",
        "perfetto/trace/power/power_rails.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/trace/power:lite_gen
cc_proto_library(
    name = "power_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:power",
    ],
)

# GN target: //protos/perfetto/trace/power:zero_gen
proto_library(
    name = "power_zero",
    srcs = [
        "perfetto/trace/power/battery_counters.proto",
        "perfetto/trace/power/power_rails.proto",
    ],
)

# GN target: //protos/perfetto/trace/power:zero_gen
pbzero_cc_proto_library(
    name = "power_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:power_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/third_party/pprof:lite_gen
proto_library(
    name = "pprof",
    srcs = [
        "third_party/pprof/profile.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/third_party/pprof:lite_gen
cc_proto_library(
    name = "pprof_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:pprof",
    ],
)

# GN target: //protos/perfetto/trace/profiling:lite_gen
proto_library(
    name = "profiling",
    srcs = [
        "perfetto/trace/profiling/profile_packet.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/trace/profiling:lite_gen
cc_proto_library(
    name = "profiling_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:profiling",
    ],
)

# GN target: //protos/perfetto/trace/profiling:zero_gen
proto_library(
    name = "profiling_zero",
    srcs = [
        "perfetto/trace/profiling/profile_packet.proto",
    ],
)

# GN target: //protos/perfetto/trace/profiling:zero_gen
pbzero_cc_proto_library(
    name = "profiling_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:profiling_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/trace/ps:lite_gen
proto_library(
    name = "ps",
    srcs = [
        "perfetto/trace/ps/process_stats.proto",
        "perfetto/trace/ps/process_tree.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/trace/ps:lite_gen
cc_proto_library(
    name = "ps_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:ps",
    ],
)

# GN target: //protos/perfetto/trace/ps:zero_gen
proto_library(
    name = "ps_zero",
    srcs = [
        "perfetto/trace/ps/process_stats.proto",
        "perfetto/trace/ps/process_tree.proto",
    ],
)

# GN target: //protos/perfetto/trace/ps:zero_gen
pbzero_cc_proto_library(
    name = "ps_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:ps_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/trace/sys_stats:lite_gen
proto_library(
    name = "sys_stats",
    srcs = [
        "perfetto/trace/sys_stats/sys_stats.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:common",
    ],
)

# GN target: //protos/perfetto/trace/sys_stats:lite_gen
cc_proto_library(
    name = "sys_stats_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:sys_stats",
    ],
)

# GN target: //protos/perfetto/trace/sys_stats:zero_gen
proto_library(
    name = "sys_stats_zero",
    srcs = [
        "perfetto/trace/sys_stats/sys_stats.proto",
    ],
    deps = [
        "//third_party/perfetto/protos:common_zero",
    ],
)

# GN target: //protos/perfetto/trace/sys_stats:zero_gen
pbzero_cc_proto_library(
    name = "sys_stats_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:sys_stats_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/trace:lite_gen
proto_library(
    name = "trace",
    srcs = [
        "perfetto/trace/test_event.proto",
        "perfetto/trace/trace.proto",
        "perfetto/trace/trace_packet.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:android",
        "//third_party/perfetto/protos:chrome",
        "//third_party/perfetto/protos:common",
        "//third_party/perfetto/protos:config",
        "//third_party/perfetto/protos:filesystem",
        "//third_party/perfetto/protos:ftrace",
        "//third_party/perfetto/protos:interned_data",
        "//third_party/perfetto/protos:power",
        "//third_party/perfetto/protos:profiling",
        "//third_party/perfetto/protos:ps",
        "//third_party/perfetto/protos:sys_stats",
        "//third_party/perfetto/protos:trace_minimal",
        "//third_party/perfetto/protos:track_event",
    ],
)

# GN target: //protos/perfetto/trace:lite_gen
cc_proto_library(
    name = "trace_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:trace",
    ],
)

# GN target: //protos/perfetto/trace:minimal_lite_gen
proto_library(
    name = "trace_minimal",
    srcs = [
        "perfetto/trace/clock_snapshot.proto",
        "perfetto/trace/system_info.proto",
        "perfetto/trace/trigger.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:common",
        "//third_party/perfetto/protos:config",
    ],
)

# GN target: //protos/perfetto/trace:minimal_lite_gen
cc_proto_library(
    name = "trace_minimal_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:trace_minimal",
    ],
)

# GN target: //protos/perfetto/trace_processor:lite_gen
proto_library(
    name = "trace_processor",
    srcs = [
        "perfetto/trace_processor/raw_query.proto",
        "perfetto/trace_processor/sched.proto",
        "perfetto/trace_processor/trace_processor.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/trace_processor:lite_gen
cc_proto_library(
    name = "trace_processor_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:trace_processor",
    ],
)

# GN target: //protos/perfetto/trace:zero_gen
proto_library(
    name = "trace_zero",
    srcs = [
        "perfetto/trace/clock_snapshot.proto",
        "perfetto/trace/system_info.proto",
        "perfetto/trace/test_event.proto",
        "perfetto/trace/trace.proto",
        "perfetto/trace/trace_packet.proto",
        "perfetto/trace/trigger.proto",
    ],
    deps = [
        "//third_party/perfetto/protos:android_zero",
        "//third_party/perfetto/protos:chrome_zero",
        "//third_party/perfetto/protos:common_zero",
        "//third_party/perfetto/protos:config_zero",
        "//third_party/perfetto/protos:filesystem_zero",
        "//third_party/perfetto/protos:ftrace_zero",
        "//third_party/perfetto/protos:interned_data_zero",
        "//third_party/perfetto/protos:power_zero",
        "//third_party/perfetto/protos:profiling_zero",
        "//third_party/perfetto/protos:ps_zero",
        "//third_party/perfetto/protos:sys_stats_zero",
        "//third_party/perfetto/protos:track_event_zero",
    ],
)

# GN target: //protos/perfetto/trace:zero_gen
pbzero_cc_proto_library(
    name = "trace_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:trace_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)

# GN target: //protos/perfetto/trace/track_event:lite_gen
proto_library(
    name = "track_event",
    srcs = [
        "perfetto/trace/track_event/debug_annotation.proto",
        "perfetto/trace/track_event/process_descriptor.proto",
        "perfetto/trace/track_event/task_execution.proto",
        "perfetto/trace/track_event/thread_descriptor.proto",
        "perfetto/trace/track_event/track_event.proto",
    ],
    has_services = 1,
    cc_api_version = 2,
    cc_generic_services = 1,
    visibility = [
        "//visibility:public",
    ],
)

# GN target: //protos/perfetto/trace/track_event:lite_gen
cc_proto_library(
    name = "track_event_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//third_party/perfetto/protos:track_event",
    ],
)

# GN target: //protos/perfetto/trace/track_event:zero_gen
proto_library(
    name = "track_event_zero",
    srcs = [
        "perfetto/trace/track_event/debug_annotation.proto",
        "perfetto/trace/track_event/process_descriptor.proto",
        "perfetto/trace/track_event/task_execution.proto",
        "perfetto/trace/track_event/thread_descriptor.proto",
        "perfetto/trace/track_event/track_event.proto",
    ],
)

# GN target: //protos/perfetto/trace/track_event:zero_gen
pbzero_cc_proto_library(
    name = "track_event_zero_cc_proto",
    src_proto_library = "//third_party/perfetto/protos:track_event_zero",
    deps = [
        "//third_party/perfetto:libprotozero",
        "//third_party/perfetto/google:gtest_prod",
    ],
)
