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

#ifndef SRC_TRACED_PROBES_FILESYSTEM_PREFIX_FINDER_H_
#define SRC_TRACED_PROBES_FILESYSTEM_PREFIX_FINDER_H_

#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace perfetto {

// This needs to have paths supplied in DFS order.
class PrefixFinder {
 public:
  // Opaque placeholder for a prefix that can be turned into a string
  // using .ToString.
  // Can not be constructed outside of PrefixFinder.
  class Node {
   public:
    friend class PrefixFinder;

    Node(const Node& that) = delete;
    Node& operator=(const Node&) = delete;

    std::string ToString();

   private:
    Node(std::string name, Node* parent) : name_(name), parent_(parent) {}

    std::string name_;
    std::map<std::string, std::unique_ptr<Node>> children_;
    Node* parent_;
  };

  PrefixFinder(size_t limit);
  void AddPath(std::string path);
  Node* GetPrefix(std::string path);

 private:
  const size_t limit_;
  // (path element, count) tuples for last path seen.
  std::vector<std::pair<std::string, size_t>> state_;
  Node root_{"", nullptr};
};

}  // namespace perfetto
#endif  // SRC_TRACED_PROBES_FILESYSTEM_PREFIX_FINDER_H_
