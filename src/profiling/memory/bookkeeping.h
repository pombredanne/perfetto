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

// Internal data-structure for Callsites to save memory if the same function
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
// different processes. Each call site is represented by a Callsites::Node
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
    Node* const parent_;
    const InternedCodeLocation location_;
    base::LookupSet<Node, const InternedCodeLocation, &Node::location_>
        children_;
  };

  Callsites() = default;
  Callsites(const Callsites&) = delete;
  Callsites& operator=(const Callsites&) = delete;

  uint64_t GetCumSizeForTesting(const std::vector<CodeLocation>& stack);
  Node* IncrementCallsite(const std::vector<CodeLocation>& locs, uint64_t size);
  static void DecrementNode(Node* node, uint64_t size);

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
  explicit HeapDump(Callsites* callsites) : callsites_(callsites) {}

  void RecordMalloc(const std::vector<CodeLocation>& stack,
                    uint64_t address,
                    uint64_t size,
                    uint64_t sequence_number);
  void RecordFree(uint64_t address, uint64_t sequence_number);

 private:
  struct Allocation {
    Allocation(uint64_t size, uint64_t seq, Callsites::Node* n)
        : alloc_size(size), sequence_number(seq), node(n) {}

    Allocation() = default;
    Allocation(const Allocation&) = delete;
    Allocation(Allocation&& other) noexcept {
      alloc_size = other.alloc_size;
      sequence_number = other.sequence_number;
      node = other.node;
      other.node = nullptr;
    }

    ~Allocation() {
      if (node)
        Callsites::DecrementNode(node, alloc_size);
    }

    uint64_t alloc_size;
    uint64_t sequence_number;
    Callsites::Node* node;
  };

  // Sequencing logic works as following:
  // * mallocs are immediately applied to the heapdump. They are rejected if
  //   the current malloc for the address has a higher sequence number.
  //
  //   If all operations with sequence numbers lower than the malloc have been
  //   applied, sequence_number_ is advanced and all unblocked
  //   pending operations are applied.
  //   Otherwise, a no-op record is added to the pending operations queue to
  //   remember an operation has been applied for the sequence number.

  // * for frees:
  //   if all operations with sequence numbers lower than the free have
  //     been applied (i.e sequence_number_ == sequence_number - 1)
  //     the free is applied and sequence_number_ is advanced.
  //     All unblocked pending operations are applied.
  //   otherwise: the free is added to the queue of pending operations.

  void AddPendingFree(uint64_t sequence_number, uint64_t address);
  void AddPendingNoop(uint64_t sequence_number) {
    AddPendingFree(sequence_number, 0);
  }

  // This must be  called after all operations up to sequence_number have been
  // applied.
  void ApplyFree(uint64_t sequence_number, uint64_t address);

  // Address -> (size, sequence_number, code location)
  std::map<uint64_t, Allocation> allocations_;
  // sequence_number -> pending operation.
  // pending operation is encoded as a integer, if != 0, the operation is a
  // pending free of the address. if == 0, the pending operation is a no-op.
  // No-op operations come from allocs that have already been applied to the
  // heap dump. It is important to keep track of them in the list of pending
  // operations in order to determine when all operations prior to a free have
  // been applied.
  std::map<uint64_t, uint64_t> pending_;
  // The sequence number all mallocs and frees have been handled up to.
  uint64_t sequence_number_ = 0;
  Callsites* const callsites_;
};

}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_BOOKKEEPING_H_
