/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <unistd.h>
#include <array>
#include <memory>
#include <vector>

#include <signal.h>

#include "perfetto/base/event.h"
#include "perfetto/base/unix_socket.h"
#include "src/profiling/memory/bounded_queue.h"
#include "src/profiling/memory/heapprofd_producer.h"
#include "src/profiling/memory/socket_listener.h"
#include "src/profiling/memory/wire_protocol.h"
#include "src/tracing/ipc/default_socket.h"
#include "src/tracing/ipc/producer/reconnecting_producer.h"

#include "perfetto/base/unix_task_runner.h"

namespace perfetto {
namespace profiling {
namespace {

/*
base::Event* g_dump_evt = nullptr;

void DumpSignalHandler(int) {
  g_dump_evt->Notify();
}
*/

// We create kUnwinderThreads unwinding threads and one bookeeping thread.
// The bookkeeping thread is singleton in order to avoid expensive and
// complicated synchronisation in the bookkeeping.
//
// We wire up the system by creating BoundedQueues between the threads. The main
// thread runs the TaskRunner driving the SocketListener. The unwinding thread
// takes the data received by the SocketListener and if it is a malloc does
// stack unwinding, and if it is a free just forwards the content of the record
// to the bookkeeping thread.
//
//             +--------------+
//             |SocketListener|
//             +------+-------+
//                    |
//          +--UnwindingRecord -+
//          |                   |
// +--------v-------+   +-------v--------+
// |Unwinding Thread|   |Unwinding Thread|
// +--------+-------+   +-------+--------+
//          |                   |
//          +-BookkeepingRecord +
//                    |
//           +--------v---------+
//           |Bookkeeping Thread|
//           +------------------+
int HeapprofdMain(int, char**) {
  base::UnixTaskRunner task_runner;
  ReconnectingProducer producer(
      "android.heapprofd", GetProducerSocket(), &task_runner,
      [&task_runner](TracingService::ProducerEndpoint* endpoint) {
        return std::unique_ptr<Producer>(
            new HeapprofdProducer(&task_runner, endpoint));
      });
  producer.ConnectWithRetries();
  task_runner.Run();
  return 0;
}

}  // namespace
}  // namespace profiling
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::profiling::HeapprofdMain(argc, argv);
}
