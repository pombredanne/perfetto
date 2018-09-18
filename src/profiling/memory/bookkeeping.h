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

#ifndef SRC_PROFILING_MEMORY_BOOKKEEPING_H_
#define SRC_PROFILING_MEMORY_BOOKKEEPING_H_

#include "perfetto/base/lookup_set.h"
#include "src/profiling/memory/string_interner.h"

#include <map>
#include <string>
#include <vector>

namespace perfetto {

class HeapDump;

struct CodeLocation {
  CodeLocation(std::string map_n, std::string function_n)
      : map_name(std::move(map_n)), function_name(std::move(function_n)) {}

  std::string map_name;
  std::string function_name;
};

// Internal data-structure for Callsites to safe memory if the same function
// is named multiple times.
struct InternedCodeLocation {
  StringInterner::InternedString map_name;
  StringInterner::InternedString function_name;

  bool operator<(const InternedCodeLocation& other) const {
    if (map_name.id() == other.map_name.id())
      return function_name.id() < other.function_name.id();
    return map_name.id() < other.map_name.id();
  }
};

// Graph of function callsites. This is shared between heap dumps for
// different processes. Each call sites is represented by a Callsites::Node
// that is owned by the parent (i.e. calling) callsite. It has a pointer
// to its parent, which means the function call-graph can be reconstructed
// from a Callsites::Node by walking down the pointers to the parents.
class Callsites {
 public:
  // Node in a tree of function traces that resulted in an allocation. For
  // instance, if alloc_buf is called from foo and bar, which are called from
  // main, the tree looks as following.
  //
  //            alloc_buf    alloc_buf
  //                   |      |
  //                  foo    bar
  //                    \    /
  //                      main
  //                       |
  //                   libc_init
  //                       |
  //                    [root_]
  //
  // allocations_ will hold a map from the pointers returned from malloc to
  // alloc_buf to the leafs of this tree.
  class Node {
   public:
    // This is opaque except to Callsites.
    friend class Callsites;

    Node(InternedCodeLocation location) : Node(std::move(location), nullptr) {}
    Node(InternedCodeLocation location, Node* parent)
        : parent_(parent), location_(std::move(location)) {}

   private:
    Node* GetOrCreateChild(const InternedCodeLocation& loc);

    uint64_t cum_size_ = 0;
    Node* parent_;
    const InternedCodeLocation location_;
    base::LookupSet<Node, const InternedCodeLocation, &Node::location_>
        children_;
  };

  uint64_t GetCumSizeForTesting(const std::vector<CodeLocation>& stack);
  Node* IncrementCallsite(const std::vector<CodeLocation>& locs, uint64_t size);
  void DecrementNode(Node* node, uint64_t size);

 private:
  InternedCodeLocation InternCodeLocation(const CodeLocation& loc) {
    return {interner_.Intern(loc.map_name),
            interner_.Intern(loc.function_name)};
  }

  StringInterner interner_;
  Node root_{{interner_.Intern(""), interner_.Intern("")}};
};

class HeapDump {
 public:
  explicit HeapDump(Callsites* bookkeeper) : callsites_(bookkeeper) {}

  void RecordMalloc(const std::vector<CodeLocation>& stack,
                    uint64_t address,
                    uint64_t size,
                    uint64_t sequence_number);
  void RecordFree(uint64_t address, uint64_t sequence_number);
  ~HeapDump();

 private:
  struct Allocation {
    Allocation(uint64_t size, uint64_t seq, Callsites::Node* n)
        : alloc_size(size), sequence_number(seq), node(n) {}

    uint64_t alloc_size;
    uint64_t sequence_number;
    Callsites::Node* node;
  };

  // If address is 0, record that an alloc was recorded at sequence_number.
  // Otherwise, record that a free of address occured at sequence_number.
  // This free will be delayed until all operations up to sequence_number
  // have been observed.
  void AddPending(uint64_t sequence_number, uint64_t address);
  // This must be  called after all operations up to sequence_number have been
  // applied.
  void ApplyFree(uint64_t sequence_number, uint64_t address);

  // Address -> (size, sequence_number, code location)
  std::map<uint64_t, Allocation> allocations_;
  // sequence number -> (allocation to free || 0 for malloc)
  std::map<uint64_t, uint64_t> pending_;
  // The sequence number all mallocs and frees have been handled up to.
  uint64_t consistent_sequence_number_ = 0;
  Callsites* callsites_;
};

}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_BOOKKEEPING_H_
