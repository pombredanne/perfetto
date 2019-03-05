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

#include "src/traced/service/lazy_producers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

class MockLazyProducers : public LazyProducers {
 public:
  MOCK_METHOD2(SetAndroidProperty,
               bool(const std::string&, const std::string&));
  MOCK_METHOD1(GetAndroidProperty, std::string(const std::string&));
};

TEST(LazyProducersTest, Simple) {
  TraceConfig cfg;
  auto ds = cfg.add_data_sources();
  ds->mutable_config()->set_name("android.heapprofd");

  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, GetAndroidProperty("persist.heapprofd.enable"))
      .WillOnce(Return(""));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "1"))
      .WillOnce(Return(true));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "0"))
      .WillOnce(Return(true));
  p.StartTracing(1, cfg);
  p.StopTracing(1);
}

TEST(LazyProducersTest, AlreadySet) {
  TraceConfig cfg;
  auto ds = cfg.add_data_sources();
  ds->mutable_config()->set_name("android.heapprofd");

  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, GetAndroidProperty("persist.heapprofd.enable"))
      .WillOnce(Return("1"));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", _)).Times(0);
  p.StartTracing(1, cfg);
  p.StopTracing(1);
}

TEST(LazyProducersTest, Failed) {
  TraceConfig cfg;
  auto ds = cfg.add_data_sources();
  ds->mutable_config()->set_name("android.heapprofd");

  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, GetAndroidProperty("persist.heapprofd.enable"))
      .WillOnce(Return(""));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "1"))
      .WillOnce(Return(false));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "0")).Times(0);
  p.StartTracing(1, cfg);
  p.StopTracing(1);
}

TEST(LazyProducersTest, Unknown) {
  TraceConfig cfg;
  auto ds = cfg.add_data_sources();
  ds->mutable_config()->set_name("android.invalidproducer");

  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, SetAndroidProperty(_, _)).Times(0);
  p.StartTracing(1, cfg);
  p.StopTracing(1);
}

TEST(LazyProducersTest, RefCount) {
  TraceConfig cfg;
  auto ds = cfg.add_data_sources();
  ds->mutable_config()->set_name("android.heapprofd");

  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, GetAndroidProperty("persist.heapprofd.enable"))
      .WillOnce(Return(""));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "1"))
      .WillOnce(Return(true));
  p.StartTracing(1, cfg);
  p.StartTracing(2, cfg);
  p.StopTracing(2);
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "0"))
      .WillOnce(Return(true));
  p.StopTracing(1);
}

}  // namespace
}  // namespace perfetto
