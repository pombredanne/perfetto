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

#include "src/profiling/memory/matcher.h"

#include "gtest/gtest.h"

namespace perfetto {
namespace profiling {
namespace {

TEST(MatcherTest, Orphans) {
  bool match = false;
  auto match_fn = [&match](const Process&, const std::vector<ProcessSet*>&) {
    match = true;
  };
  bool shutdown = false;
  auto shutdown_fn = [&shutdown](pid_t) { shutdown = true; };

  Matcher m(std::move(shutdown_fn), std::move(match_fn));
  auto handle = m.NotifyProcess({1, "init"});
  EXPECT_FALSE(shutdown);
  m.GarbageCollectOrphans();
  EXPECT_FALSE(shutdown);
  m.GarbageCollectOrphans();
  EXPECT_TRUE(shutdown);
  EXPECT_FALSE(match);
}

TEST(MatcherTest, MatchPIDProcessSetFirst) {
  bool match = false;
  auto match_fn = [&match](const Process&, const std::vector<ProcessSet*>&) {
    match = true;
  };
  bool shutdown = false;
  auto shutdown_fn = [&shutdown](pid_t) { shutdown = true; };

  Matcher m(std::move(shutdown_fn), std::move(match_fn));
  ProcessSet ps;
  ps.data_source = nullptr;  // Does not matter for this test.
  ps.pids.emplace(1);

  auto ps_handle = m.AwaitProcessSet(std::move(ps));
  auto handle = m.NotifyProcess({1, "init"});
  EXPECT_TRUE(match);
  m.GarbageCollectOrphans();
  m.GarbageCollectOrphans();
  EXPECT_FALSE(shutdown);
}

TEST(MatcherTest, MatchPIDProcessSetSecond) {
  bool match = false;
  auto match_fn = [&match](const Process&, const std::vector<ProcessSet*>&) {
    match = true;
  };
  bool shutdown = false;
  auto shutdown_fn = [&shutdown](pid_t) { shutdown = true; };

  Matcher m(std::move(shutdown_fn), std::move(match_fn));
  ProcessSet ps;
  ps.data_source = nullptr;  // Does not matter for this test.
  ps.pids.emplace(1);

  auto handle = m.NotifyProcess({1, "init"});
  auto ps_handle = m.AwaitProcessSet(std::move(ps));
  EXPECT_TRUE(match);
  m.GarbageCollectOrphans();
  m.GarbageCollectOrphans();
  EXPECT_FALSE(shutdown);
}

TEST(MatcherTest, MatchCmdlineProcessSetFirst) {
  bool match = false;
  auto match_fn = [&match](const Process&, const std::vector<ProcessSet*>&) {
    match = true;
  };
  bool shutdown = false;
  auto shutdown_fn = [&shutdown](pid_t) { shutdown = true; };

  Matcher m(std::move(shutdown_fn), std::move(match_fn));
  ProcessSet ps;
  ps.data_source = nullptr;  // Does not matter for this test.
  ps.process_cmdline.emplace("init");

  auto ps_handle = m.AwaitProcessSet(std::move(ps));
  auto handle = m.NotifyProcess({1, "init"});
  EXPECT_TRUE(match);
  m.GarbageCollectOrphans();
  m.GarbageCollectOrphans();
  EXPECT_FALSE(shutdown);
}

TEST(MatcherTest, MatchCmdlineProcessSetSecond) {
  bool match = false;
  auto match_fn = [&match](const Process&, const std::vector<ProcessSet*>&) {
    match = true;
  };
  bool shutdown = false;
  auto shutdown_fn = [&shutdown](pid_t) { shutdown = true; };

  Matcher m(std::move(shutdown_fn), std::move(match_fn));
  ProcessSet ps;
  ps.data_source = nullptr;  // Does not matter for this test.
  ps.process_cmdline.emplace("init");

  auto handle = m.NotifyProcess({1, "init"});
  auto ps_handle = m.AwaitProcessSet(std::move(ps));
  EXPECT_TRUE(match);
  m.GarbageCollectOrphans();
  m.GarbageCollectOrphans();
  EXPECT_FALSE(shutdown);
}

TEST(MatcherTest, ExpiredProcessSetHandle) {
  bool match = false;
  auto match_fn = [&match](const Process&, const std::vector<ProcessSet*>&) {
    match = true;
  };
  bool shutdown = false;
  auto shutdown_fn = [&shutdown](pid_t) { shutdown = true; };

  Matcher m(std::move(shutdown_fn), std::move(match_fn));
  ProcessSet ps;
  ps.data_source = nullptr;  // Does not matter for this test.
  ps.pids.emplace(1);

  { auto ps_handle = m.AwaitProcessSet(std::move(ps)); }
  auto handle = m.NotifyProcess({1, "init"});
  EXPECT_FALSE(match);
  m.GarbageCollectOrphans();
  m.GarbageCollectOrphans();
}

TEST(MatcherTest, ExpiredProcessHandle) {
  bool match = false;
  auto match_fn = [&match](const Process&, const std::vector<ProcessSet*>&) {
    match = true;
  };
  bool shutdown = false;
  auto shutdown_fn = [&shutdown](pid_t) { shutdown = true; };

  Matcher m(std::move(shutdown_fn), std::move(match_fn));
  ProcessSet ps;
  ps.data_source = nullptr;  // Does not matter for this test.
  ps.pids.emplace(1);

  { auto handle = m.NotifyProcess({1, "init"}); }
  EXPECT_FALSE(shutdown);
  auto ps_handle = m.AwaitProcessSet(std::move(ps));
  EXPECT_FALSE(match);
  m.GarbageCollectOrphans();
  m.GarbageCollectOrphans();
}

}  // namespace
}  // namespace profiling
}  // namespace perfetto
