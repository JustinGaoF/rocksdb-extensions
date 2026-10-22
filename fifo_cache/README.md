# RocksDB FIFOCache Extension

`FIFOCache` is a RocksDB block cache (`rocksdb::Cache`) that evicts with
S3-FIFO, from [*FIFO Queues are All You Need for Cache
Eviction*](https://jasony.me/publication/sosp23-s3fifo.pdf) (SOSP '23):

- New entries enter a small probation queue. Entries read only once, and
  scans, leave through it without disturbing the hot set.
- Probation entries that were hit again are promoted to the resident queue,
  which evicts with a second-chance clock.
- Keys recently evicted from probation are remembered (keys only, no
  values); re-inserting one goes straight to resident.
- Hits take no lock: a lookup bumps a per-entry counter and moves nothing.
  Inserts, erases and eviction take a per-shard mutex.

`Cache::Priority` is ignored: placement follows access frequency. The header
[`FIFOCache.h`](include/rocksdb_extensions/fifo_cache/FIFOCache.h) documents
the options, memory accounting and how to choose `num_shard_bits`.

The API is experimental and may change.

## Use

From C++:

```cpp
#include "rocksdb_extensions/fifo_cache/FIFOCache.h"

facebook::rocks::FIFOCacheOptions opts;
opts.capacity = size_t{4} << 30;
std::shared_ptr<rocksdb::Cache> cache = opts.MakeSharedCache();

rocksdb::BlockBasedTableOptions table_options;
table_options.block_cache = cache;
```

From a string, after registering the factory once at startup:

```cpp
facebook::rocks::RegisterFIFOCache(*rocksdb::ObjectLibrary::Default(), "");

std::shared_ptr<rocksdb::Cache> cache;  // must start empty
rocksdb::Status s = rocksdb::Cache::CreateFromString(
    rocksdb::ConfigOptions(), "fifo_cache://capacity=4G;num_shard_bits=6",
    &cache);
```

This is the path `db_bench --cache_uri` takes. A nested
`block_cache={fifo_cache://...}` inside an options string is not supported:
there RocksDB parses the value as options for the block cache already set
(the default one) before the `fifo_cache://` factory is consulted. Set
`block_cache` in C++ instead.

The string form takes `capacity` (required), `num_shard_bits`,
`strict_capacity_limit`, `metadata_charge_policy`, `hash_seed`,
`use_adaptive_mutex` and `max_capacity`. A bad value fails with
`InvalidArgument`.

Not supported here: `secondary_cache` (RocksDB's adapter for it is internal;
`MakeSharedCache` returns `nullptr` when it is set).

## Build

The extension needs only RocksDB (built with RTTI, which RocksDB's CMake
enables for every build type except `Release` unless `-DUSE_RTTI=1` is set)
and, for the tests, GTest.

```bash
cmake -S . -B build -GNinja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DROCKSDB_EXTENSIONS_WITH_NIMBLE=OFF \
  -DROCKSDB_EXTENSIONS_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH=/path/to/rocksdb/prefix
cmake --build build
ctest --test-dir build --output-on-failure
```

`-DROCKSDB_EXTENSIONS_WITH_NIMBLE=OFF` skips the Nimble extension and its
dependencies. Add `-DROCKSDB_EXTENSIONS_FETCH_DEPS=ON` to fetch RocksDB and
GTest when no package is installed. Link `rocksdb_extensions_fifo_cache`.

## Build into RocksDB and benchmark with db_bench

This directory is also a RocksDB plugin ([`fifo_cache.mk`](fifo_cache.mk)).
Built that way, FIFOCache is compiled into `librocksdb` and registered at
startup, so RocksDB's own tools accept the string form:

```bash
ln -s /path/to/rocksdb-extensions/fifo_cache /path/to/rocksdb/plugin/fifo_cache
cd /path/to/rocksdb
cmake -S . -B build -DROCKSDB_PLUGINS=fifo_cache -DCMAKE_BUILD_TYPE=Release
cmake --build build --target db_bench
./build/db_bench --benchmarks=fillseq,readrandom \
  --cache_uri='fifo_cache://capacity=4294967296;num_shard_bits=6'
```

## Tests

`tests/FIFOCacheTest.cpp` covers the eviction policy, the lock-free lookup
path and its memory reclamation, capacity and table accounting, pinning, the
option string and thread exit. Tests that steer interleavings with sync
points run only in builds without `NDEBUG`.
