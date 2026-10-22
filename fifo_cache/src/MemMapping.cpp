// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under both the GPLv2 (found in the
// COPYING file in the root directory) and Apache 2.0 License
// (found in the LICENSE.Apache file in the root directory).

#include "MemMapping.h"

#include <cassert>
#include <cstdint>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace facebook::rocks::fifo_cache {

MemMapping::~MemMapping() {
#ifdef _WIN32
  if (addr_ != nullptr) {
    (void)::UnmapViewOfFile(addr_);
  }
  if (page_file_handle_ != nullptr) {
    (void)::CloseHandle(page_file_handle_);
  }
#else
  if (addr_ != nullptr) {
    [[maybe_unused]] int status = munmap(addr_, length_);
    assert(status == 0);
  }
#endif
}

MemMapping::MemMapping(MemMapping&& other) noexcept {
  *this = std::move(other);
}

MemMapping& MemMapping::operator=(MemMapping&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  // Releases the current mapping on return.
  MemMapping old;
  old.addr_ = addr_;
  old.length_ = length_;
  addr_ = other.addr_;
  length_ = other.length_;
  other.addr_ = nullptr;
  other.length_ = 0;
#ifdef _WIN32
  old.page_file_handle_ = page_file_handle_;
  page_file_handle_ = other.page_file_handle_;
  other.page_file_handle_ = nullptr;
#endif
  return *this;
}

MemMapping MemMapping::AllocateLazyZeroedNoReserve(size_t length) {
  MemMapping mm;
  mm.length_ = length;
  if (length == 0) {
    return mm;
  }
#ifdef _WIN32
  // Windows commits the page file backing up front; no equivalent.
  const uint64_t len64 = length;
  mm.page_file_handle_ = ::CreateFileMapping(
      INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT,
      static_cast<DWORD>(len64 >> 32), static_cast<DWORD>(len64), nullptr);
  if (mm.page_file_handle_ == nullptr) {
    return mm;
  }
  mm.addr_ =
      ::MapViewOfFile(mm.page_file_handle_, FILE_MAP_WRITE, 0, 0, length);
#else
  int no_reserve_flag = 0;
#ifdef MAP_NORESERVE
  no_reserve_flag = MAP_NORESERVE;
#endif
  mm.addr_ = mmap(nullptr, length, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | no_reserve_flag, -1, 0);
  if (mm.addr_ == MAP_FAILED) {
    mm.addr_ = nullptr;
  }
#endif
  return mm;
}

}  // namespace facebook::rocks::fifo_cache
