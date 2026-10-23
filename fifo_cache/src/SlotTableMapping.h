// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under both the GPLv2 (found in the
// COPYING file in the root directory) and Apache 2.0 License
// (found in the LICENSE.Apache file in the root directory).

// The slot table's address reservation. The reference implementation uses
// MemMapping::AllocateLazyZeroedNoReserve, which the pinned RocksDB release
// does not provide (it has only AllocateLazyZeroed). This is the smallest
// replacement: on Linux it maps the table MAP_NORESERVE itself, so a large
// reservation does not count against the commit limit; elsewhere it holds a
// MemMapping::AllocateLazyZeroed, exactly what the NoReserve variant does
// off Linux. Only Get() is used, once, to set the table base.

#pragma once

#include <cstddef>
#include <utility>

#include "port/mmap.h"

#ifdef OS_LINUX
#include <sys/mman.h>
#endif

namespace facebook::rocks::fifo_cache {

class SlotTableMapping {
 public:
  static SlotTableMapping Allocate(size_t length) {
    SlotTableMapping m;
#ifdef OS_LINUX
    m.length_ = length;
    if (length == 0) {
      return m;
    }
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    void* p = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (p == MAP_FAILED) {
      m.length_ = 0;
      return m;
    }
    m.addr_ = p;
#else
    m.owned_ = ROCKSDB_NAMESPACE::MemMapping::AllocateLazyZeroed(length);
    m.addr_ = m.owned_.Get();
    m.length_ = m.owned_.Length();
#endif
    return m;
  }

  SlotTableMapping(const SlotTableMapping&) = delete;
  SlotTableMapping& operator=(const SlotTableMapping&) = delete;

  SlotTableMapping(SlotTableMapping&& other) noexcept
      : addr_(other.addr_), length_(other.length_) {
    other.addr_ = nullptr;
    other.length_ = 0;
#ifndef OS_LINUX
    owned_ = std::move(other.owned_);
#endif
  }

  // Move-assign is not needed (the shard's table is set once) and is deleted
  // to avoid assigning over the owned mapping.
  SlotTableMapping& operator=(SlotTableMapping&&) = delete;

  ~SlotTableMapping() {
#ifdef OS_LINUX
    if (addr_ != nullptr) {
      ::munmap(addr_, length_);
    }
#endif
  }

  void* Get() const { return addr_; }

 private:
  SlotTableMapping() = default;

  void* addr_ = nullptr;
  size_t length_ = 0;
#ifndef OS_LINUX
  ROCKSDB_NAMESPACE::MemMapping owned_;
#endif
};

}  // namespace facebook::rocks::fifo_cache
