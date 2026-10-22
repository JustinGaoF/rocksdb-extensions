// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under both the GPLv2 (found in the
// COPYING file in the root directory) and Apache 2.0 License
// (found in the LICENSE.Apache file in the root directory).

// Test hooks, after RocksDB's test_util/sync_point.h (not an installed
// header), reduced to the callbacks FIFOCache's tests use. Compiled out of
// release (NDEBUG) builds, as in RocksDB.

#pragma once

#ifdef NDEBUG
#define TEST_SYNC_POINT(x)
#define TEST_SYNC_POINT_CALLBACK(x, y)
#else

#include <functional>
#include <string>

namespace facebook::rocks::fifo_cache {

class SyncPoint {
 public:
  static SyncPoint* GetInstance();

  SyncPoint(const SyncPoint&) = delete;
  SyncPoint& operator=(const SyncPoint&) = delete;
  SyncPoint(SyncPoint&&) = delete;
  SyncPoint& operator=(SyncPoint&&) = delete;
  ~SyncPoint();

  // Runs callback(arg) whenever point is reached while processing is on.
  void SetCallBack(const std::string& point,
                   const std::function<void(void*)>& callback);
  void ClearAllCallBacks();
  void EnableProcessing();
  void DisableProcessing();

  // Called by TEST_SYNC_POINT and TEST_SYNC_POINT_CALLBACK.
  void Process(const char* point, void* cb_arg = nullptr);

 private:
  SyncPoint();
  struct Data;
  Data* impl_;
};

}  // namespace facebook::rocks::fifo_cache

#define TEST_SYNC_POINT(x) \
  ::facebook::rocks::fifo_cache::SyncPoint::GetInstance()->Process(x)
#define TEST_SYNC_POINT_CALLBACK(x, y) \
  ::facebook::rocks::fifo_cache::SyncPoint::GetInstance()->Process(x, y)

#endif  // NDEBUG
