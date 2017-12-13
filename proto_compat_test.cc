
#include "perfetto/base/logging.h"
#include "protos/tracing_service/trace_config.pb.h"

using namespace ::perfetto::protos;

int main() {
  // Step 1: Generate a future version of the TraceConfig.
  std::string encoded_from_future;
  {
    TraceConfigFromTheFuture tc;
    auto* buf_cfg = tc.add_buffers();
    buf_cfg->set_size_kb(1234);
    buf_cfg->set_futuristic_int(42);
    buf_cfg->set_fill_policy(
        TraceConfigFromTheFuture::BufferConfig::FUTURISTIC_POLICY);
    tc.set_futuristic_string("bazinga");
    encoded_from_future = tc.SerializeAsString();
  }

  // Step 2: Decode and re-encode using an older version of TraceConfig.
  std::string re_encoded_from_the_past;
  {
    TraceConfig tc;
    bool decoded = tc.ParseFromString(encoded_from_future);
    PERFETTO_CHECK(decoded);
    auto* buf_cfg = tc.add_buffers();
    buf_cfg->set_size_kb(9999);
    re_encoded_from_the_past = tc.SerializeAsString();
  }

  // Step 3: Decode using the newer version and check that we preserved the
  // fields from the future that were not defined in the legacy TraceConfig.
  {
    TraceConfigFromTheFuture tc;
    bool decoded = tc.ParseFromString(re_encoded_from_the_past);
    PERFETTO_CHECK(decoded);
    PERFETTO_CHECK(tc.buffers_size() == 2);

    PERFETTO_CHECK(tc.buffers(0).size_kb() == 1234);
    PERFETTO_CHECK(tc.buffers(0).futuristic_int() == 42);
    PERFETTO_CHECK(tc.buffers(0).fill_policy() == TraceConfigFromTheFuture::BufferConfig::FUTURISTIC_POLICY);
    PERFETTO_CHECK(tc.futuristic_string() == "bazinga");

    PERFETTO_CHECK(tc.buffers(1).size_kb() == 9999);
  }
}
