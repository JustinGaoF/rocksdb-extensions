// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under both the GPLv2 (found in the
// COPYING file in the root directory) and Apache 2.0 License
// (found in the LICENSE.Apache file in the root directory).

// Small platform and bit helpers. RocksDB has equivalents under port/ and
// util/, but those headers are not installed, so an extension built against
// RocksDB's public API carries its own. Behavior matches the RocksDB
// versions the cache was written against.

#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <type_traits>

#if !defined(_WIN32)
#include <pthread.h>
#endif

// ASan (and valgrind) want every heap allocation freed at exit.
#if defined(__clang__)
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ROCKSDB_EXT_MUST_FREE_HEAP_ALLOCATIONS 1
#endif
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define ROCKSDB_EXT_MUST_FREE_HEAP_ALLOCATIONS 1
#endif
#ifdef ROCKSDB_VALGRIND_RUN
#define ROCKSDB_EXT_MUST_FREE_HEAP_ALLOCATIONS 1
#endif

// One spelling of "built with TSan" for clang and GCC.
#if defined(__clang__)
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define ROCKSDB_EXT_TSAN 1
#endif
#endif
#elif defined(__SANITIZE_THREAD__)
#define ROCKSDB_EXT_TSAN 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ROCKSDB_EXT_LIKELY(x) (__builtin_expect(!!(x), 1))
#define ROCKSDB_EXT_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define ROCKSDB_EXT_LIKELY(x) (x)
#define ROCKSDB_EXT_UNLIKELY(x) (x)
#endif

namespace facebook::rocks::fifo_cache {

#ifdef ROCKSDB_EXT_MUST_FREE_HEAP_ALLOCATIONS
constexpr bool kMustFreeHeapAllocations = true;
#else
constexpr bool kMustFreeHeapAllocations = false;
#endif

// Same values as RocksDB's CACHE_LINE_SIZE.
#if defined(__s390__)
constexpr size_t kCacheLineSize = 256;
#elif defined(__powerpc__) || defined(__aarch64__)
constexpr size_t kCacheLineSize = 128;
#else
constexpr size_t kCacheLineSize = 64;
#endif

// A 128-bit key or hash as two 64-bit halves, as RocksDB's UniqueId64x2.
using UniqueId64x2 = std::array<uint64_t, 2>;
constexpr UniqueId64x2 kNullUniqueId64x2 = {};

inline uint32_t Lower32of64(uint64_t v) { return static_cast<uint32_t>(v); }
inline uint32_t Upper32of64(uint64_t v) {
  return static_cast<uint32_t>(v >> 32);
}

// The low nbits bits of v; nbits < 64.
inline uint64_t BottomNBits(uint64_t v, int nbits) {
  assert(nbits >= 0 && nbits < 64);
  return v & ((uint64_t{1} << nbits) - 1);
}

// Index of the highest set bit; v > 0.
template <typename T>
inline int FloorLog2(T v) {
  static_assert(std::is_unsigned_v<T>);
  assert(v > 0);
  return std::bit_width(v) - 1;
}

template <typename T>
inline int BitsSetToOne(T v) {
  static_assert(std::is_unsigned_v<T>);
  return std::popcount(v);
}

// A cast that asserts the value is unchanged.
template <typename To, typename From>
inline To lossless_cast(From x) {
  To result = static_cast<To>(x);
  assert(static_cast<From>(result) == x);
  return result;
}

// static_cast, checked with dynamic_cast in debug builds.
template <class DestClass, class SrcClass>
inline DestClass* static_cast_with_check(SrcClass* x) {
  DestClass* ret = static_cast<DestClass*>(x);
#ifndef NDEBUG
  assert(ret == dynamic_cast<DestClass*>(x));
#endif
  return ret;
}

inline uint64_t EndianSwapValue(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap64(v);
#else
  return ((v & 0x00000000000000ffULL) << 56) |
         ((v & 0x000000000000ff00ULL) << 40) |
         ((v & 0x0000000000ff0000ULL) << 24) |
         ((v & 0x00000000ff000000ULL) << 8) |
         ((v & 0x000000ff00000000ULL) >> 8) |
         ((v & 0x0000ff0000000000ULL) >> 24) |
         ((v & 0x00ff000000000000ULL) >> 40) |
         ((v & 0xff00000000000000ULL) >> 56);
#endif
}

// 64x64 -> 128-bit product as (high, low).
inline void Multiply64to128(uint64_t a, uint64_t b, uint64_t* hi,
                            uint64_t* lo) {
#if defined(__SIZEOF_INT128__)
  const unsigned __int128 p = static_cast<unsigned __int128>(a) * b;
  *hi = static_cast<uint64_t>(p >> 64);
  *lo = static_cast<uint64_t>(p);
#else
  const uint64_t a_lo = Lower32of64(a);
  const uint64_t a_hi = Upper32of64(a);
  const uint64_t b_lo = Lower32of64(b);
  const uint64_t b_hi = Upper32of64(b);
  const uint64_t lo_lo = a_lo * b_lo;
  const uint64_t hi_lo = a_hi * b_lo;
  const uint64_t lo_hi = a_lo * b_hi;
  const uint64_t hi_hi = a_hi * b_hi;
  const uint64_t cross = (lo_lo >> 32) + Lower32of64(hi_lo) + lo_hi;
  *hi = hi_hi + (hi_lo >> 32) + (cross >> 32);
  *lo = (cross << 32) | Lower32of64(lo_lo);
#endif
}

// RocksDB's BijectiveHash2x64 (adapted from XXH3_len_9to16_128b): a
// bijection on 128 bits for a given seed, so a 16-byte key can be stored as
// its hash and recovered exactly.
inline void BijectiveHash2x64(uint64_t in_high64, uint64_t in_low64,
                              uint64_t seed, uint64_t* out_high64,
                              uint64_t* out_low64) {
  const uint64_t bitflipl = /*secret part*/ 0x59973f0033362349U - seed;
  const uint64_t bitfliph = /*secret part*/ 0xc202797692d63d58U + seed;
  uint64_t hi = 0;
  uint64_t lo = 0;
  Multiply64to128(in_low64 ^ in_high64 ^ bitflipl, 0x9E3779B185EBCA87U, &hi,
                  &lo);
  lo += 0x3c0000000000000U;  // (len - 1) << 54
  in_high64 ^= bitfliph;
  hi += in_high64 + (Lower32of64(in_high64) * uint64_t{0x85EBCA76});
  lo ^= EndianSwapValue(hi);
  uint64_t hi2 = 0;
  Multiply64to128(lo, 0xC2B2AE3D27D4EB4FU, &hi2, &lo);
  hi = hi2 + (hi * 0xC2B2AE3D27D4EB4FU);
  auto avalanche = [](uint64_t h64) {
    h64 ^= h64 >> 37;
    h64 *= 0x165667919E3779F9U;
    h64 ^= h64 >> 32;
    return h64;
  };
  *out_low64 = avalanche(lo);
  *out_high64 = avalanche(hi);
}

// The inverse of BijectiveHash2x64 for the same seed.
inline void BijectiveUnhash2x64(uint64_t in_high64, uint64_t in_low64,
                                uint64_t seed, uint64_t* out_high64,
                                uint64_t* out_low64) {
  auto unavalanche = [](uint64_t h64) {
    h64 ^= h64 >> 32;
    h64 *= 0x8da8ee41d6df849U;  // inverse of 0x165667919E3779F9U
    h64 ^= h64 >> 37;
    return h64;
  };
  auto upper_of_product = [](uint64_t a, uint64_t b) {
    uint64_t hi = 0;
    uint64_t lo = 0;
    Multiply64to128(a, b, &hi, &lo);
    return hi;
  };
  const uint64_t bitflipl = /*secret part*/ 0x59973f0033362349U - seed;
  const uint64_t bitfliph = /*secret part*/ 0xc202797692d63d58U + seed;
  uint64_t lo = unavalanche(in_low64);
  uint64_t hi = unavalanche(in_high64);
  lo *= 0xba79078168d4baf;  // inverse of 0xC2B2AE3D27D4EB4FU
  hi -= upper_of_product(lo, 0xC2B2AE3D27D4EB4FU);
  hi *= 0xba79078168d4baf;  // inverse of 0xC2B2AE3D27D4EB4FU
  lo ^= EndianSwapValue(hi);
  lo -= 0x3c0000000000000U;
  lo *= 0x887493432badb37U;  // inverse of 0x9E3779B185EBCA87U
  hi -= upper_of_product(lo, 0x9E3779B185EBCA87U);
  uint32_t tmp32 = Lower32of64(hi) * 0xb6c92f47;  // inverse of 0x85EBCA77
  hi -= tmp32;
  hi = (hi & 0xFFFFFFFF00000000U) -
       ((tmp32 * uint64_t{0x85EBCA76}) & 0xFFFFFFFF00000000U) + tmp32;
  hi ^= bitfliph;
  lo ^= hi ^ bitflipl;
  *out_high64 = hi;
  *out_low64 = lo;
}

// Seed-0 forms, as RocksDB's: FIFOCache folds its seed into the input.
inline void BijectiveHash2x64(uint64_t in_high64, uint64_t in_low64,
                              uint64_t* out_high64, uint64_t* out_low64) {
  BijectiveHash2x64(in_high64, in_low64, /*seed*/ 0, out_high64, out_low64);
}
inline void BijectiveUnhash2x64(uint64_t in_high64, uint64_t in_low64,
                                uint64_t* out_high64, uint64_t* out_low64) {
  BijectiveUnhash2x64(in_high64, in_low64, /*seed*/ 0, out_high64, out_low64);
}

// A seeded 128-bit hash of arbitrary bytes, for keys that are not 16 bytes
// long. RocksDB uses XXH3-128 here; only the distribution matters (such keys
// are compared in full), so this folds 16-byte blocks through the bijection
// above instead of carrying another hash library.
inline void Hash2x64(const char* data, size_t n, uint64_t seed,
                     uint64_t* high64, uint64_t* low64) {
  uint64_t h = 0x9E3779B97F4A7C15U ^ (uint64_t{n} * 0xC2B2AE3D27D4EB4FU);
  uint64_t l = seed;
  size_t i = 0;
  for (; i + 16 <= n; i += 16) {
    uint64_t a = 0;
    uint64_t b = 0;
    std::memcpy(&a, data + i, 8);
    std::memcpy(&b, data + i + 8, 8);
    BijectiveHash2x64(h ^ b, l ^ a, seed, &h, &l);
  }
  if (i < n || n == 0) {
    char tail[16] = {};
    std::memcpy(tail, data + i, n - i);
    uint64_t a = 0;
    uint64_t b = 0;
    std::memcpy(&a, tail, 8);
    std::memcpy(&b, tail + 8, 8);
    BijectiveHash2x64(h ^ b ^ (n - i), l ^ a, seed, &h, &l);
  }
  *high64 = h;
  *low64 = l;
}

// Cache-line-aligned allocation for the shard array.
inline void* CachelineAlignedAlloc(size_t size) {
  return ::operator new(size, std::align_val_t{kCacheLineSize});
}
inline void CachelineAlignedFree(void* p) {
  ::operator delete(p, std::align_val_t{kCacheLineSize});
}

// The shard mutex: pthread, adaptive where glibc offers it, as RocksDB's
// port::Mutex (what DMutex is without folly).
class Mutex {
 public:
  explicit Mutex(bool adaptive) {
#if !defined(_WIN32)
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
    if (adaptive) {
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
    }
#else
    (void)adaptive;
#endif
    pthread_mutex_init(&mu_, &attr);
    pthread_mutexattr_destroy(&attr);
#else
    (void)adaptive;
#endif
  }
  ~Mutex() {
#if !defined(_WIN32)
    pthread_mutex_destroy(&mu_);
#endif
  }
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
  Mutex(Mutex&&) = delete;
  Mutex& operator=(Mutex&&) = delete;

  void Lock() {
#if !defined(_WIN32)
    pthread_mutex_lock(&mu_);
#else
    mu_.lock();
#endif
  }
  void Unlock() {
#if !defined(_WIN32)
    pthread_mutex_unlock(&mu_);
#else
    mu_.unlock();
#endif
  }

 private:
#if !defined(_WIN32)
  pthread_mutex_t mu_;
#else
  std::mutex mu_;
#endif
};

class MutexLock {
 public:
  explicit MutexLock(Mutex& mu) : mu_(mu) { mu_.Lock(); }
  ~MutexLock() { mu_.Unlock(); }
  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;
  MutexLock(MutexLock&&) = delete;
  MutexLock& operator=(MutexLock&&) = delete;

 private:
  Mutex& mu_;
};

}  // namespace facebook::rocks::fifo_cache
