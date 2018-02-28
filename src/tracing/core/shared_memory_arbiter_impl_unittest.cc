/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "src/tracing/core/shared_memory_arbiter_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "perfetto/base/utils.h"
#include "perfetto/tracing/core/basic_types.h"
#include "perfetto/tracing/core/commit_data_request.h"
#include "perfetto/tracing/core/shared_memory_abi.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "src/base/test/test_task_runner.h"
#include "src/tracing/test/aligned_buffer_test.h"

namespace perfetto {
namespace {

using testing::Invoke;
using testing::_;

class MockProducerEndpoint : public Service::ProducerEndpoint {
 public:
  void RegisterDataSource(const DataSourceDescriptor&,
                          RegisterDataSourceCallback) override {}
  void UnregisterDataSource(DataSourceID) override {}
  SharedMemory* shared_memory() const override { return nullptr; }
  std::unique_ptr<TraceWriter> CreateTraceWriter(BufferID) override {
    return nullptr;
  }

  MOCK_METHOD1(CommitData, void(const CommitDataRequest&));
};

class SharedMemoryArbiterImplTest : public AlignedBufferTest {
 public:
  void SetUp() override {
    AlignedBufferTest::SetUp();
    task_runner_.reset(new base::TestTaskRunner());
    arbiter_.reset(new SharedMemoryArbiterImpl(buf(), buf_size(), page_size(),
                                               &mock_producer_endpoint_,
                                               task_runner_.get()));
  }

  void TearDown() override {
    arbiter_.reset();
    task_runner_.reset();
  }

  std::unique_ptr<base::TestTaskRunner> task_runner_;
  std::unique_ptr<SharedMemoryArbiterImpl> arbiter_;
  MockProducerEndpoint mock_producer_endpoint_;
  std::function<void(const std::vector<uint32_t>&)> on_pages_complete_;
};

size_t const kPageSizes[] = {4096, 65536};
INSTANTIATE_TEST_CASE_P(PageSize,
                        SharedMemoryArbiterImplTest,
                        ::testing::ValuesIn(kPageSizes));

// The buffer has 14 pages (kNumPages), each will be partitioned in 14 chunks.
// The test requests all 14 * 14 chunks, alternating amongst 14 target buf IDs.
// Because a chunk can share a page only if all other chunks in the page have
// the same target buffer ID, there is only one possible final distribution:
// each page is filled with chunks that all belong to the same buffer ID.

TODO fix this test,
    not true anymore.

        TEST_P(SharedMemoryArbiterImplTest, GetAndReturnChunks) {
  SharedMemoryArbiterImpl::set_default_layout_for_testing(
      SharedMemoryABI::PageLayout::kPageDiv14);
  static constexpr size_t kTotChunks = kNumPages * 14;
  SharedMemoryABI::Chunk chunks[kTotChunks];
  for (size_t i = 0; i < kTotChunks; i++) {
    chunks[i] = arbiter_->GetNewChunk({}, 0 /*size_hint*/);
    ASSERT_TRUE(chunks[i].is_valid());
  }

  SharedMemoryABI* abi = arbiter_->shmem_abi_for_testing();
  for (size_t page_idx = 0; page_idx < kNumPages; page_idx++) {
    ASSERT_FALSE(abi->is_page_free(page_idx));
    ASSERT_EQ(0u, abi->GetFreeChunks(page_idx));
    const uint32_t page_layout = abi->page_layout_dbg(page_idx);
    ASSERT_EQ(14u, SharedMemoryABI::GetNumChunksForLayout(page_layout));
    for (uint8_t chunk_idx = 0; chunk_idx < 14; chunk_idx++) {
      auto chunk = abi->GetChunkUnchecked(page_idx, page_layout, chunk_idx);
      ASSERT_TRUE(chunk.is_valid());
    }
  }

  // Finally return just two pages marking all their chunks as complete, and
  // check that the notification callback is posted.

  auto on_callback = task_runner_->CreateCheckpoint("on_callback");
  EXPECT_CALL(mock_producer_endpoint_, CommitData(_))
      .WillOnce(Invoke([on_callback](const CommitDataRequest& req) {
        PERFETTO_ELOG("aaa");
        ASSERT_EQ(2, req.chunks_to_move_size());
        ASSERT_EQ(0u, req.chunks_to_move()[0].page_number());
        ASSERT_EQ(41u, req.chunks_to_move()[0].target_buffer());
        ASSERT_EQ(3u, req.chunks_to_move()[1].page_number());
        ASSERT_EQ(42u, req.chunks_to_move()[1].target_buffer());
        // TODO(primiano): ASSERT_EQ on buffer and chunk number.
        on_callback();
      }));
  for (size_t i = 0; i < 14; i++) {
    arbiter_->ReturnCompletedChunk(std::move(chunks[14 * i]), 41);
    arbiter_->ReturnCompletedChunk(std::move(chunks[14 * i + 3]), 42);
  }
  task_runner_->RunUntilCheckpoint("on_callback");
}

// Check that we can actually create up to kMaxWriterID TraceWriter(s).
TEST_P(SharedMemoryArbiterImplTest, WriterIDsAllocation) {
  std::map<WriterID, std::unique_ptr<TraceWriter>> writers;
  for (size_t i = 0; i < kMaxWriterID; i++) {
    std::unique_ptr<TraceWriter> writer = arbiter_->CreateTraceWriter(0);
    ASSERT_TRUE(writer);
    WriterID writer_id = writer->writer_id();
    ASSERT_TRUE(writers.emplace(writer_id, std::move(writer)).second);
  }

  // A further call should fail as we exhausted writer IDs.
  ASSERT_EQ(nullptr, arbiter_->CreateTraceWriter(0).get());
}

// TODO(primiano): add multi-threaded tests.

}  // namespace
}  // namespace perfetto
