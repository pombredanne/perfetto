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

#include "src/profiling/memory/bookkeeping.h"

namespace perfetto {

Callsites::Node* Callsites::Node::GetOrCreateChild(
    const InternedCodeLocation& loc) {
  Node* child = children_.Get(loc);
  if (!child)
    child = children_.Emplace(loc, this);
  return child;
}

void HeapDump::RecordMalloc(const std::vector<CodeLocation>& locs,
                            uint64_t address,
                            uint64_t size,
                            uint64_t sequence_number) {
  auto it = allocations_.find(address);
  if (it != allocations_.end()) {
    if (it->second.sequence_number > sequence_number)
      return;
    else
      // Clean up previous allocation.
      ApplyFree(it->second.sequence_number + 1, address);
  }

  Callsites::Node* node = callsites_->IncrementCallsite(locs, size);
  allocations_.emplace(address, Allocation(size, sequence_number, node));
  AddPending(sequence_number, 0);
}

void HeapDump::RecordFree(uint64_t address, uint64_t sequence_number) {
  AddPending(sequence_number, address);
}

void HeapDump::ApplyFree(uint64_t sequence_number, uint64_t address) {
  auto leaf_it = allocations_.find(address);
  if (leaf_it == allocations_.end())
    return;

  const Allocation& value = leaf_it->second;
  if (value.sequence_number > sequence_number)
    return;
  callsites_->DecrementNode(value.node, value.alloc_size);
  allocations_.erase(leaf_it);
}

void HeapDump::AddPending(uint64_t sequence_number, uint64_t address) {
  if (sequence_number != consistent_sequence_number_ + 1) {
    pending_.emplace(sequence_number, address);
    return;
  }

  if (address)
    ApplyFree(sequence_number, address);
  consistent_sequence_number_++;

  auto it = pending_.upper_bound(0);
  while (it != pending_.end() && it->first == consistent_sequence_number_ + 1) {
    if (it->second)
      ApplyFree(it->first, it->second);
    consistent_sequence_number_++;
    it = pending_.erase(it);
  }
}

HeapDump::~HeapDump() {
  auto it = allocations_.begin();
  while (it != allocations_.end()) {
    callsites_->DecrementNode(it->second.node, it->second.alloc_size);
    it = allocations_.erase(it);
  }
}

uint64_t Callsites::GetCumSizeForTesting(
    const std::vector<CodeLocation>& locs) {
  Node* node = &root_;
  for (const CodeLocation& loc : locs) {
    node = node->children_.Get(InternCodeLocation(loc));
    if (node == nullptr)
      return 0;
  }
  return node->cum_size_;
}

Callsites::Node* Callsites::IncrementCallsite(
    const std::vector<CodeLocation>& locs,
    uint64_t size) {
  Node* node = &root_;
  node->cum_size_ += size;
  for (const CodeLocation& loc : locs) {
    node = node->GetOrCreateChild(InternCodeLocation(loc));
    node->cum_size_ += size;
  }
  return node;
}

void Callsites::DecrementNode(Node* node, uint64_t size) {
  bool delete_prev = false;
  Node* prev = nullptr;
  while (node != nullptr) {
    if (delete_prev)
      node->children_.Remove(*prev);
    node->cum_size_ -= size;
    delete_prev = node->cum_size_ == 0;
    prev = node;
    node = node->parent_;
  }
}

}  // namespace perfetto
