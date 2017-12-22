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

#include <jni.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "perfetto/tracing/ipc/producer_ipc_client.h"

#include "protos/test_event.pbzero.h"
#include "protos/trace_packet.pbzero.h"

namespace perfetto {

namespace {

class ProducerImpl : public Producer {
 public:
  ProducerImpl(const std::string name,
               base::UnixTaskRunner* task_runner)
      : name_(name),
        endpoint_(ProducerIPCClient::Connect(PERFETTO_PRODUCER_SOCK_NAME,
                                             this,
                                             task_runner)),
        task_runner_(task_runner) {}
  ~ProducerImpl() override = default;

  void OnConnect() override {
    PERFETTO_ILOG("connected");
    DataSourceDescriptor descriptor;
    descriptor.set_name(name_);
    endpoint_->RegisterDataSource(descriptor,
                                  [this](DataSourceID id) { id_ = id; });
  }

  void OnDisconnect() override {
    PERFETTO_ILOG("Disconnect");
    Shutdown();
  }

  void CreateDataSourceInstance(
      DataSourceInstanceID,
      const DataSourceConfig& source_config) override {
    PERFETTO_ILOG("Create");
    const std::string& categories = source_config.trace_category_filters();
    if (categories != "foo,bar") {
      Shutdown();
      return;
    }

    PERFETTO_ILOG("Writing");
    auto trace_writer = endpoint_->CreateTraceWriter(1);
    for (int i = 0; i < 10; i++) {
      auto handle = trace_writer->NewTracePacket();
      handle->set_test("test");
      handle->Finalize();
    }

    // Temporarily create a new packet to flush the final packet to the
    // consumer.
    // TODO(primiano): remove this hack once flushing the final packet is fixed.
    trace_writer->NewTracePacket();

    PERFETTO_ILOG("Finalized");
    endpoint_->UnregisterDataSource(id_);
  }

  void TearDownDataSourceInstance(DataSourceInstanceID) override {
    PERFETTO_ILOG("Teardown");
    Shutdown();
  }

 private:
  void Shutdown() {
    endpoint_.reset();
    task_runner_->Quit();
  }

  std::string name_;
  DataSourceID id_;
  std::unique_ptr<Service::ProducerEndpoint> endpoint_;
  base::UnixTaskRunner* task_runner_;
};

void ListenAndRespond(const std::string& name) {
  base::UnixTaskRunner task_runner;
  ProducerImpl producer(name, &task_runner);
  task_runner.Run();
}

}  // namespace

}  // namespace perfetto

extern "C" JNIEXPORT void JNICALL
Java_android_perfetto_producer_ProducerActivity_setupProducer(
    JNIEnv*,
    jclass /*clazz*/) {
  PERFETTO_ILOG("JNI");
  perfetto::ListenAndRespond("android.perfetto.cts.ProducerActivity");
}

extern "C" JNIEXPORT void JNICALL
Java_android_perfetto_producer_ProducerIsolatedService_setupProducer(
    JNIEnv*,
    jclass /*clazz*/) {
  PERFETTO_ILOG("JNI");
  perfetto::ListenAndRespond("android.perfetto.cts.ProducerIsolatedService");
}


extern "C" JNIEXPORT void JNICALL
Java_android_perfetto_producer_ProducerService_setupProducer(
    JNIEnv*,
    jclass /*clazz*/) {
  PERFETTO_ILOG("JNI");
  perfetto::ListenAndRespond("android.perfetto.cts.ProducerService");
}
