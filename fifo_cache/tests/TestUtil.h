// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under both the GPLv2 (found in the
// COPYING file in the root directory) and Apache 2.0 License
// (found in the LICENSE.Apache file in the root directory).

// The few RocksDB test helpers (test_util/testharness.h, util/random.h,
// neither installed) that FIFOCache's tests use.

#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>

#include "rocksdb/status.h"

namespace facebook::rocks::fifo_cache::test {

inline ::testing::AssertionResult AssertStatus(
    const char* s_expr, const ROCKSDB_NAMESPACE::Status& s) {
  if (s.ok()) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << s_expr << "\n" << s.ToString();
}

}  // namespace facebook::rocks::fifo_cache::test

// Each evaluates its argument once.
#define ASSERT_OK(s) \
  ASSERT_PRED_FORMAT1(::facebook::rocks::fifo_cache::test::AssertStatus, s)
#define EXPECT_OK(s) \
  EXPECT_PRED_FORMAT1(::facebook::rocks::fifo_cache::test::AssertStatus, s)

// Passes the test with a note, for a case that does not apply here.
#define ROCKSDB_GTEST_BYPASS(m)         \
  do {                                  \
    fputs("BYPASSED: " m "\n", stderr); \
    GTEST_SUCCESS_("BYPASSED: " m);     \
  } while (false)

namespace facebook::rocks::fifo_cache {

// RocksDB's Random: a Lehmer generator, so a seed gives the same sequence
// as in RocksDB's tests.
class Random {
 private:
  enum : uint32_t {
    M = 2147483647L  // 2^31-1
  };
  enum : uint64_t {
    A = 16807  // bits 14, 8, 7, 5, 2, 1, 0
  };

  uint32_t seed_;

  static uint32_t GoodSeed(uint32_t s) { return (s & M) != 0 ? (s & M) : 1; }

 public:
  explicit Random(uint32_t s) : seed_(GoodSeed(s)) {}

  uint32_t Next() {
    uint64_t product = seed_ * A;
    seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
    if (seed_ > M) {
      seed_ -= M;
    }
    return seed_;
  }

  // A uniformly distributed value in [0, n-1]; n > 0.
  uint32_t Uniform(int n) { return Next() % n; }

  // True about 1/n of the time; n > 0.
  bool OneIn(int n) { return Uniform(n) == 0; }

  // A number in [0, 2^max_log - 1] with exponential bias towards smaller
  // numbers.
  uint32_t Skewed(int max_log) { return Uniform(1 << Uniform(max_log + 1)); }
};

}  // namespace facebook::rocks::fifo_cache
