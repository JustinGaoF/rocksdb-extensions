// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under both the GPLv2 (found in the
// COPYING file in the root directory) and Apache 2.0 License
// (found in the LICENSE.Apache file in the root directory).

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "rocksdb/cache.h"

namespace ROCKSDB_NAMESPACE {
class ObjectLibrary;
}  // namespace ROCKSDB_NAMESPACE

namespace facebook::rocks {

// EXPERIMENTAL: the API and the fifo_cache:// option string are subject to
// change.
// FIFOCache - A RocksDB block cache using segmented FIFO eviction (S3-FIFO,
// SOSP '23) to stay at or below a set capacity. The cache is sharded to
// 2^num_shard_bits shards, by hash of the key. The total capacity is divided
// and evenly assigned to each shard, and each shard has two FIFO queues for
// evictions: new entries enter probation, and probation victims with enough
// access frequency are promoted to resident (second-chance) instead of being
// evicted. Keys (not values) recently evicted from probation are remembered,
// and re-inserting one goes directly to resident. Placement is inferred
// from access frequency: Cache::Priority is ignored. Lookups take no lock;
// inserts, erases and eviction take a per-shard mutex. Pinned entries are
// never evicted; eviction sets long-pinned entries aside instead of
// rescanning them, and a fully-pinned shard evicts nothing.
// The memory that remembers recently evicted keys (a few bytes plus one
// hash-map entry per remembered key, at most one remembered key per
// cached entry, or more for a while after entries are erased) is not
// charged against capacity; it stays at its peak size after SetCapacity
// lowers capacity.
// Under kFullChargeCacheMetadata (the default) the slot table also counts
// against capacity: 64 bytes per slot the table has grown to, 256 bytes
// per shard at construction, growing with the entries, and shrinking back
// once the shard is emptied (by EraseUnRefEntries, or by a capacity cut that
// evicts every entry). A shard needs well over that much capacity to hold any
// entry. Each shard reserves address space for max(2048, shard capacity / 64)
// slots of 64 bytes up front, using max_capacity instead of capacity when it is
// larger, and touches it as the table grows. On Linux the reservation does not
// count against the commit limit unless vm.overcommit_memory=2; on Windows it
// is committed up front. If it cannot be reserved, construction aborts, as for
// HyperClockCache. The table cannot grow past its reservation, which holds
// about one entry per 107 bytes of reserved capacity: once SetCapacity raises
// capacity beyond that (about 38x for 4 KiB entries, with the default
// max_capacity), new keys are also evicted by occupancy. Each shard also keeps
// one cache line per thread that has used it (allocated 64 threads at a time,
// for up to 8192 threads), which is not charged against capacity.
//
// Choosing num_shard_bits: hits take no lock, so more shards only spread
// inserts (misses) over more locks, while fewer, larger shards share space
// more evenly. In db_bench and cache_bench runs, the default (up to 64
// shards) was best or tied from 256 MiB up; fewer shards were faster only
// for a cache much smaller than its hot set, and very small shards were
// slower. So keep the default unless the cache is much smaller than its hot
// set.
//
// Creating one: call MakeSharedCache, or pass "fifo_cache://capacity=<n>;..."
// (the scalar fields below) to Cache::CreateFromString with an empty result,
// as db_bench --cache_uri does. When RocksDB is built with this plugin, the
// fifo_cache:// factory is registered automatically; when linking the
// extension library instead, call RegisterFIFOCache once at startup. A nested
// block_cache={fifo_cache://...} in an options string is not supported: a
// plugin cannot add the core parsing branch that trims the braces, so set
// block_cache in C++ there. memory_allocator and secondary_cache can only be
// set in C++.
struct FIFOCacheOptions : public ROCKSDB_NAMESPACE::ShardedCacheOptions {
  // Whether to use adaptive mutexes for cache shards. Note that adaptive
  // mutexes need to be supported by the platform in order for this to have any
  // effect. The default value is true if RocksDB is compiled with
  // -DROCKSDB_DEFAULT_TO_ADAPTIVE_MUTEX, false otherwise.
  bool use_adaptive_mutex = ROCKSDB_NAMESPACE::kDefaultToAdaptiveMutex;

  // Capacity the slot table is reserved for, when larger than capacity (see
  // above). Set it to the largest capacity SetCapacity may raise this cache
  // to; 0 (the default) reserves for capacity. On Linux a larger
  // reservation costs only address space until the table grows into it; on
  // Windows it is committed up front.
  size_t max_capacity = 0;

  FIFOCacheOptions() {}
  // Every argument is required, so a positional call written for another
  // cache's options does not compile.
  FIFOCacheOptions(
      size_t _capacity, int _num_shard_bits, bool _strict_capacity_limit,
      std::shared_ptr<ROCKSDB_NAMESPACE::MemoryAllocator> _memory_allocator,
      bool _use_adaptive_mutex,
      ROCKSDB_NAMESPACE::CacheMetadataChargePolicy _metadata_charge_policy)
      : ROCKSDB_NAMESPACE::ShardedCacheOptions(
            _capacity, _num_shard_bits, _strict_capacity_limit,
            std::move(_memory_allocator), _metadata_charge_policy),
        use_adaptive_mutex(_use_adaptive_mutex) {}

  // Construct an instance of FIFOCache using these options. Returns nullptr
  // if num_shard_bits >= 20.
  std::shared_ptr<ROCKSDB_NAMESPACE::Cache> MakeSharedCache() const;
};

// Registers the "fifo_cache://" form with library, so that
// Cache::CreateFromString and option strings naming a block cache can create
// a FIFOCache. A bad option string fails with InvalidArgument. Typically
// called once at startup on *ROCKSDB_NAMESPACE::ObjectLibrary::Default().
// Returns the number of factories registered. arg is unused (the RocksDB
// plugin registrar signature).
int RegisterFIFOCache(ROCKSDB_NAMESPACE::ObjectLibrary& library,
                      const std::string& arg);

}  // namespace facebook::rocks

// RegisterFIFOCache with C linkage, the form RocksDB's plugin build
// (ROCKSDB_PLUGINS=fifo_cache) calls at startup.
extern "C" int rocksdb_extensions_fifo_cache_register(
    ROCKSDB_NAMESPACE::ObjectLibrary& library, const std::string& arg);
