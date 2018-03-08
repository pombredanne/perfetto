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

#ifndef TEST_FAKE_PRODUCER_H_
#define TEST_FAKE_PRODUCER_H_

#include <memory>
#include <random>
#include <string>

#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/ipc/producer_ipc_client.h"
#include "src/base/test/test_task_runner.h"

namespace perfetto {

class FakeProducer : public Producer {
 public:
  explicit FakeProducer(const std::string& name);
  ~FakeProducer() override;

  void Connect(const char* socket_name,
               base::TaskRunner* task_runner,
               std::function<void()> data_produced_callback);

  // Producer implementation.
  void OnConnect() override;
  void OnDisconnect() override;
  void CreateDataSourceInstance(DataSourceInstanceID,
                                const DataSourceConfig& source_config) override;
  void TearDownDataSourceInstance(DataSourceInstanceID) override;

 private:
  class PacketBatchingHelper {
   public:
    PacketBatchingHelper(base::TaskRunner* task_runner,
                         std::unique_ptr<TraceWriter> writer,
                         const TestConfig& config,
                         std::function<void()> data_produced_callback);
    void SendBatch();

   private:
    base::TaskRunner* task_runner_ = nullptr;

    std::unique_ptr<TraceWriter> writer_;
    std::minstd_rand0 random_;
    size_t batches_remaining_ = 0;
    const size_t last_batch_;
    std::function<void()> data_produced_callback_;
  };

  void Shutdown();

  std::string name_;
  DataSourceID id_ = 0;

  std::unique_ptr<Service::ProducerEndpoint> endpoint_;
  base::TaskRunner* task_runner_ = nullptr;
  std::function<void()> data_produced_callback_;
  std::unique_ptr<PacketBatchingHelper> batching_helper_;
};

}  // namespace perfetto

#endif  // TEST_FAKE_PRODUCER_H_
