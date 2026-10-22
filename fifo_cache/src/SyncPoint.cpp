// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under both the GPLv2 (found in the
// COPYING file in the root directory) and Apache 2.0 License
// (found in the LICENSE.Apache file in the root directory).

#include "SyncPoint.h"

#ifndef NDEBUG

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace facebook::rocks::fifo_cache {

struct SyncPoint::Data {
  std::atomic<bool> enabled{false};
  std::mutex mutex;
  std::condition_variable cv;
  std::unordered_map<std::string, std::function<void(void*)>> callbacks;
  // Callbacks running now, so ClearAllCallBacks can wait them out.
  int num_running = 0;
};

SyncPoint* SyncPoint::GetInstance() {
  static SyncPoint instance;
  return &instance;
}

SyncPoint::SyncPoint() : impl_(new Data) {}

SyncPoint::~SyncPoint() { delete impl_; }

void SyncPoint::SetCallBack(const std::string& point,
                            const std::function<void(void*)>& callback) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->callbacks[point] = callback;
}

void SyncPoint::ClearAllCallBacks() {
  std::unique_lock<std::mutex> lock(impl_->mutex);
  impl_->cv.wait(lock, [this] { return impl_->num_running == 0; });
  impl_->callbacks.clear();
}

void SyncPoint::EnableProcessing() { impl_->enabled.store(true); }

void SyncPoint::DisableProcessing() { impl_->enabled.store(false); }

void SyncPoint::Process(const char* point, void* cb_arg) {
  if (!impl_->enabled.load(std::memory_order_relaxed)) {
    return;
  }
  std::function<void(void*)> callback;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->callbacks.find(point);
    if (it == impl_->callbacks.end()) {
      return;
    }
    callback = it->second;
    ++impl_->num_running;
  }
  callback(cb_arg);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    --impl_->num_running;
  }
  impl_->cv.notify_all();
}

}  // namespace facebook::rocks::fifo_cache

#endif  // NDEBUG
