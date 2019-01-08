/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "perfetto/tracing/core/local_trace_writer_proxy.h"

#include "gtest/gtest.h"
#include "perfetto/tracing/core/tracing_service.h"
#include "src/base/test/test_task_runner.h"
#include "src/tracing/core/shared_memory_arbiter_impl.h"
#include "src/tracing/test/aligned_buffer_test.h"
#include "src/tracing/test/fake_producer_endpoint.h"

#include "perfetto/trace/test_event.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace {

class LocalTraceWriterProxyTest : public AlignedBufferTest {
 public:
  void SetUp() override {
    SharedMemoryArbiterImpl::set_default_layout_for_testing(
        SharedMemoryABI::PageLayout::kPageDiv4);
    AlignedBufferTest::SetUp();
    task_runner_.reset(new base::TestTaskRunner());
    arbiter_.reset(new SharedMemoryArbiterImpl(buf(), buf_size(), page_size(),
                                               &fake_producer_endpoint_,
                                               task_runner_.get()));
  }

  void TearDown() override {
    arbiter_.reset();
    task_runner_.reset();
  }

  void WritePackets(LocalTraceWriterProxy* proxy, size_t packet_count) {
    for (size_t i = 0; i < packet_count; i++) {
      auto scoped_lock = proxy->BeginWrite();
      auto packet = proxy->NewTracePacket();
      packet->set_for_testing()->set_str("foo");
    }
  }

  void VerifyPacketCount(size_t expected_count) {
    SharedMemoryABI* abi = arbiter_->shmem_abi_for_testing();
    size_t packets_count = 0;
    ChunkID current_max_chunk_id = 0;
    for (size_t page_idx = 0; page_idx < kNumPages; page_idx++) {
      uint32_t page_layout = abi->page_layout_dbg(page_idx);
      size_t num_chunks = SharedMemoryABI::GetNumChunksForLayout(page_layout);
      for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
        auto chunk_state = abi->GetChunkState(page_idx, chunk_idx);
        ASSERT_TRUE(chunk_state == SharedMemoryABI::kChunkFree ||
                    chunk_state == SharedMemoryABI::kChunkComplete);
        auto chunk = abi->TryAcquireChunkForReading(page_idx, chunk_idx);
        if (!chunk.is_valid())
          continue;

        // Should only see new chunks with IDs larger than the previous read
        // since our reads and writes are serialized.
        ChunkID chunk_id = chunk.header()->chunk_id.load();
        if (last_read_max_chunk_id_ != 0)
          EXPECT_LT(last_read_max_chunk_id_, chunk_id);
        current_max_chunk_id = std::max(current_max_chunk_id, chunk_id);

        auto packets_header = chunk.header()->packets.load();
        packets_count += packets_header.count;
        if (packets_header.flags &
            SharedMemoryABI::ChunkHeader::kFirstPacketContinuesFromPrevChunk) {
          // Don't count fragmented packets twice.
          packets_count--;
        }
        abi->ReleaseChunkAsFree(std::move(chunk));
      }
    }
    last_read_max_chunk_id_ = current_max_chunk_id;
    EXPECT_EQ(expected_count, packets_count);
  }

  FakeProducerEndpoint fake_producer_endpoint_;
  std::unique_ptr<base::TestTaskRunner> task_runner_;
  std::unique_ptr<SharedMemoryArbiterImpl> arbiter_;
  std::function<void(const std::vector<uint32_t>&)> on_pages_complete_;

  ChunkID last_read_max_chunk_id_ = 0;
};

size_t const kPageSizes[] = {4096, 65536};
INSTANTIATE_TEST_CASE_P(PageSize,
                        LocalTraceWriterProxyTest,
                        ::testing::ValuesIn(kPageSizes));

TEST_P(LocalTraceWriterProxyTest, CreateUnboundAndBind) {
  // Create an unbound proxy.
  std::unique_ptr<LocalTraceWriterProxy> proxy(new LocalTraceWriterProxy());

  // Bind it right away without having written any data before.
  const BufferID kBufId = 42;
  arbiter_->CreateProxiedTraceWriter(proxy.get(), kBufId);

  const size_t kNumPackets = 32;
  WritePackets(proxy.get(), kNumPackets);
  // Finalizes the last packet and returns the chunk.
  proxy.reset();

  VerifyPacketCount(kNumPackets);
}

TEST_P(LocalTraceWriterProxyTest, CreateBound) {
  // Create a bound proxy immediately.
  const BufferID kBufId = 42;
  std::unique_ptr<LocalTraceWriterProxy> proxy(
      new LocalTraceWriterProxy(arbiter_->CreateTraceWriter(kBufId)));

  const size_t kNumPackets = 32;
  WritePackets(proxy.get(), kNumPackets);
  // Finalizes the last packet and returns the chunk.
  proxy.reset();

  VerifyPacketCount(kNumPackets);
}

TEST_P(LocalTraceWriterProxyTest, WriteWhileUnboundAndDiscard) {
  // Create an unbound proxy.
  std::unique_ptr<LocalTraceWriterProxy> proxy(new LocalTraceWriterProxy());

  const size_t kNumPackets = 32;
  WritePackets(proxy.get(), kNumPackets);

  // Should discard the written data.
  proxy.reset();

  VerifyPacketCount(0);
}

TEST_P(LocalTraceWriterProxyTest, WriteWhileUnboundAndBind) {
  // Create an unbound proxy.
  std::unique_ptr<LocalTraceWriterProxy> proxy(new LocalTraceWriterProxy());

  const size_t kNumPackets = 32;
  WritePackets(proxy.get(), kNumPackets);

  // Binding the proxy should cause the previously written packets to be written
  // to the SMB and committed.
  const BufferID kBufId = 42;
  arbiter_->CreateProxiedTraceWriter(proxy.get(), kBufId);

  VerifyPacketCount(kNumPackets);

  // Any further packets should be written to the SMB directly.
  const size_t kNumAdditionalPackets = 16;
  WritePackets(proxy.get(), kNumAdditionalPackets);
  // Finalizes the last packet and returns the chunk.
  proxy.reset();

  VerifyPacketCount(kNumAdditionalPackets);
}

TEST_P(LocalTraceWriterProxyTest, WriteMultipleChunksWhileUnboundAndBind) {
  // Create an unbound proxy.
  std::unique_ptr<LocalTraceWriterProxy> proxy(new LocalTraceWriterProxy());

  // Write a single packet to determine its size in the buffer.
  WritePackets(proxy.get(), 1);
  size_t packet_size = proxy->used_buffer_size();

  // Write at least 3 pages worth of packets.
  const size_t kNumPackets = (page_size() * 3 + packet_size - 1) / packet_size;
  WritePackets(proxy.get(), kNumPackets);

  // Binding the proxy should cause the previously written packets to be written
  // to the SMB and committed.
  const BufferID kBufId = 42;
  arbiter_->CreateProxiedTraceWriter(proxy.get(), kBufId);

  VerifyPacketCount(kNumPackets + 1);

  // Any further packets should be written to the SMB directly.
  const size_t kNumAdditionalPackets = 16;
  WritePackets(proxy.get(), kNumAdditionalPackets);
  // Finalizes the last packet and returns the chunk.
  proxy.reset();

  VerifyPacketCount(kNumAdditionalPackets);
}

}  // namespace
}  // namespace perfetto
