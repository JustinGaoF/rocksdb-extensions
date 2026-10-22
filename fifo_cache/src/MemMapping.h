// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under both the GPLv2 (found in the
// COPYING file in the root directory) and Apache 2.0 License
// (found in the LICENSE.Apache file in the root directory).

// Anonymous memory mappings, after RocksDB's port/mmap.h (not an installed
// header). Only the lazily zeroed, no-reserve form the slot table uses.

#pragma once

#include <cstddef>
#include <utility>

namespace facebook::rocks::fifo_cache {

// An owned anonymous mapping, unmapped on destruction.
class MemMapping {
 public:
  // Zero-initialized memory that the OS maps to physical memory only as it
  // is touched. On Linux the mapping does not count against the commit limit
  // (MAP_NORESERVE; Linux ignores it under vm.overcommit_memory=2, which
  // accounts the full length up front). On Windows the full length is
  // committed up front. Get() is nullptr on failure.
  static MemMapping AllocateLazyZeroedNoReserve(size_t length);

  MemMapping(const MemMapping&) = delete;
  MemMapping& operator=(const MemMapping&) = delete;
  MemMapping(MemMapping&&) noexcept;
  MemMapping& operator=(MemMapping&&) noexcept;
  ~MemMapping();

  void* Get() const { return addr_; }
  size_t Length() const { return length_; }

 protected:
  MemMapping() = default;

 private:
  void* addr_ = nullptr;
  size_t length_ = 0;
#ifdef _WIN32
  void* page_file_handle_ = nullptr;
#endif
};

// The mapping presented as an array of T.
template <typename T>
class TypedMemMapping : public MemMapping {
 public:
  /*implicit*/ TypedMemMapping(MemMapping&& v) noexcept
      : MemMapping(std::move(v)) {}

  T* Get() const { return static_cast<T*>(MemMapping::Get()); }
  size_t Count() const { return MemMapping::Length() / sizeof(T); }
};

}  // namespace facebook::rocks::fifo_cache
