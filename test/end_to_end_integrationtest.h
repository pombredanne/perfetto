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

#ifndef TEST_END_TO_END_INTEGRATIONTEST_
#define TEST_END_TO_END_INTEGRATIONTEST_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "perfetto/base/unix_task_runner.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"

#include "protos/test_event.pbzero.h"
#include "protos/trace_packet.pb.h"
#include "protos/trace_packet.pbzero.h"

using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::_;

namespace perfetto {

class MockConsumer : public Consumer {
 public:
  MockConsumer() {}
  ~MockConsumer() override {}

  // Consumer implementation.
  MOCK_METHOD0(OnConnect, void());
  MOCK_METHOD0(OnDisconnect, void());
  MOCK_METHOD2(DoOnTraceData, void(std::vector<TracePacket>*, bool));

  void OnTraceData(std::vector<TracePacket> packets, bool value) override {
    DoOnTraceData(&packets, value);
  }
};

}  // namespace perfetto

#endif // TEST_END_TO_END_INTEGRATIONTEST_