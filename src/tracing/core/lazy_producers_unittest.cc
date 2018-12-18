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

#include "src/tracing/core/lazy_producers.h"

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
  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, GetAndroidProperty("persist.heapprofd.enable"))
      .WillOnce(Return(""));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "1"))
      .WillOnce(Return(true));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", ""))
      .WillOnce(Return(true));
  auto h = p.EnableProducer("android.heapprofd");
}

TEST(LazyProducersTest, AlreadySet) {
  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, GetAndroidProperty("persist.heapprofd.enable"))
      .WillOnce(Return("1"));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", _)).Times(0);
  auto h = p.EnableProducer("android.heapprofd");
}

TEST(LazyProducersTest, Failed) {
  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, GetAndroidProperty("persist.heapprofd.enable"))
      .WillOnce(Return(""));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "1"))
      .WillOnce(Return(false));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "")).Times(0);
  auto h = p.EnableProducer("android.heapprofd");
}

TEST(LazyProducersTest, Unknown) {
  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, SetAndroidProperty(_, _)).Times(0);
  auto h = p.EnableProducer("android.invalidproducer");
}

TEST(LazyProducersTest, RefCount) {
  MockLazyProducers p;
  InSequence s;
  EXPECT_CALL(p, GetAndroidProperty("persist.heapprofd.enable"))
      .WillOnce(Return(""));
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", "1"))
      .WillOnce(Return(true));
  auto h = p.EnableProducer("android.heapprofd");
  { auto h2 = p.EnableProducer("android.heapprofd"); }
  EXPECT_CALL(p, SetAndroidProperty("persist.heapprofd.enable", ""))
      .WillOnce(Return(true));
}

}  // namespace
}  // namespace perfetto
