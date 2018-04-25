#include "src/ftrace_reader/cpu_stats_parser.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "perfetto/trace/trace_packet.pb.h"
#include "perfetto/trace/trace_packet.pbzero.h"
#include "src/tracing/core/trace_writer_for_testing.h"

namespace perfetto {
namespace {

TEST(CpuStatsParserTest, DumpCpu) {
  std::string text = R"(entries: 1
overrun: 2
commit overrun: 3
bytes: 4
oldest event ts:     5123.000
now ts:  6123.123
dropped events: 7
read events: 8
)";

  std::unique_ptr<TraceWriterForTesting> writer =
      std::unique_ptr<TraceWriterForTesting>(new TraceWriterForTesting());
  {
    auto packet = writer->NewTracePacket();
    auto* stats = packet->set_ftrace_stats()->add_cpu_stats();
    EXPECT_TRUE(DumpCpuStats(text, stats));
  }

  std::unique_ptr<protos::TracePacket> result_packet = writer->ParseProto();
  auto result = result_packet->ftrace_stats().cpu_stats(0);
  EXPECT_EQ(result.entries(), 1);
  EXPECT_EQ(result.overrun(), 2);
  EXPECT_EQ(result.commit_overrun(), 3);
  EXPECT_EQ(result.bytes_read(), 4);
  EXPECT_EQ(result.oldest_event_ts(), 5123.000);
  EXPECT_EQ(result.now_ts(), 6123.123);
  EXPECT_EQ(result.dropped_events(), 7);
  EXPECT_EQ(result.read_events(), 8);
}

}  // namespace
}  // namespace perfetto
