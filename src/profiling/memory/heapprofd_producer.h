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

#ifndef SRC_PROFILING_MEMORY_HEAPPROFD_PRODUCER_H_
#define SRC_PROFILING_MEMORY_HEAPPROFD_PRODUCER_H_

#include "perfetto/base/task_runner.h"
#include "perfetto/tracing/core/basic_types.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/tracing_service.h"
#include "src/profiling/memory/bounded_queue.h"
#include "src/profiling/memory/socket_listener.h"

#include <map>

namespace perfetto {
namespace profiling {

class HeapprofdProducer : public Producer {
 public:
  HeapprofdProducer(base::TaskRunner* task_runner,
                    TracingService::ProducerEndpoint* endpoint);
  ~HeapprofdProducer() override;

  // Producer Impl:
  void OnConnect() override;
  void OnDisconnect() override;
  void SetupDataSource(DataSourceInstanceID, const DataSourceConfig&) override;
  void StartDataSource(DataSourceInstanceID, const DataSourceConfig&) override;
  void StopDataSource(DataSourceInstanceID) override;
  void OnTracingSetup() override;
  void Flush(FlushRequestID,
             const DataSourceInstanceID* data_source_ids,
             size_t num_data_sources) override;

 private:
  std::function<void(UnwindingRecord)> MakeSocketListenerCallback();
  std::vector<BoundedQueue<UnwindingRecord>> MakeUnwinderQueues(size_t n);
  std::vector<std::thread> MakeUnwindingThreads(size_t n);
  class DataSource {
   public:
    DataSource(const DataSourceConfig&) {}
    void Start(base::TaskRunner*) {}
    void Flush() {}
  };
  base::TaskRunner* const task_runner_;
  TracingService::ProducerEndpoint* const endpoint_;

  std::map<DataSourceInstanceID, DataSource> data_sources_;

  BoundedQueue<BookkeepingRecord> bookkeeping_queue_;
  BookkeepingThread bookkeeping_thread_;
  std::vector<BoundedQueue<UnwindingRecord>> unwinder_queues_;
  std::vector<std::thread> unwinding_threads_;
  SocketListener socket_listener_;
  std::unique_ptr<base::UnixSocket> socket_;
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_HEAPPROFD_PRODUCER_H_
