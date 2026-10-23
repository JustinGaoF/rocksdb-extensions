# RocksDB FIFOCache Plugin

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

This directory is a RocksDB plugin: it holds only FIFOCache's own code plus
the build files, and it includes RocksDB's own headers for everything else
(no copied RocksDB code). The one thing the pinned release does not provide
is the `MAP_NORESERVE` slot-table mapping; see `src/SlotTableMapping.h`.

## Requirements

- RocksDB source at the revision pinned in
  [`dependencies.lock`](../../dependencies.lock) (v11.8.1). The plugin
  includes RocksDB's internal headers, so a source tree is required; an
  installed RocksDB package alone is not enough.
- CMake 3.24+ and Ninja (or GNU make), clang or GCC.
- RocksDB Release builds disable RTTI by default; pass `-DUSE_RTTI=1`
  (CMake) so the `fifo_cache://` option string and `dynamic_cast` keep
  working.

## Build RocksDB with the plugin

Link this directory into a RocksDB checkout as `plugin/fifo_cache` and build
with `ROCKSDB_PLUGINS=fifo_cache`. With CMake:

```bash
ln -s /path/to/rocksdb-extensions/fifo_cache /path/to/rocksdb/plugin/fifo_cache
cd /path/to/rocksdb
cmake -S . -B build -DROCKSDB_PLUGINS=fifo_cache \
  -DCMAKE_BUILD_TYPE=Release -DUSE_RTTI=1
cmake --build build --target db_bench -j
```

The plugin's sources compile into `librocksdb` and the `fifo_cache://`
factory registers itself at startup. [`fifo_cache.mk`](fifo_cache.mk) also
carries the `SOURCES`/`TESTS`/`FUNC` variables for RocksDB's Makefile build.

## Use from C++

Include the plugin's header and add `fifo_cache/include` to the include
path:

```cpp
#include "rocksdb/advanced_cache.h"
#include "rocksdb/table.h"
#include "rocksdb_extensions/fifo_cache/FIFOCache.h"

facebook::rocks::FIFOCacheOptions opts;
opts.capacity = size_t{4} << 30;
std::shared_ptr<rocksdb::Cache> cache = opts.MakeSharedCache();

rocksdb::BlockBasedTableOptions table_options;
table_options.block_cache = cache;
```

## Use without changing application code

When RocksDB is built with the plugin, the `fifo_cache://` factory is
already registered, so any tool that takes a cache URI can use FIFOCache
with no code changes. For example, `db_bench` (which does not reference
FIFOCache) takes it through `--cache_uri`:

```bash
./build/db_bench --benchmarks=fillseq,readrandom \
  --cache_uri='fifo_cache://capacity=4294967296;num_shard_bits=6'
```

The same string form works from C++ through `Cache::CreateFromString`
with an empty target. When linking the extension library below instead of
building the plugin into RocksDB, register the factory once at startup
first:

```cpp
#include "rocksdb/advanced_cache.h"
#include "rocksdb/convenience.h"
#include "rocksdb/utilities/object_registry.h"
#include "rocksdb_extensions/fifo_cache/FIFOCache.h"

facebook::rocks::RegisterFIFOCache(*rocksdb::ObjectLibrary::Default(), "");

std::shared_ptr<rocksdb::Cache> cache;  // must start empty
rocksdb::Status s = rocksdb::Cache::CreateFromString(
    rocksdb::ConfigOptions(), "fifo_cache://capacity=4G;num_shard_bits=6",
    &cache);
```

The string form takes `capacity` (required), `num_shard_bits`,
`strict_capacity_limit`, `metadata_charge_policy`, `hash_seed`,
`use_adaptive_mutex` and `max_capacity`. A bad value fails with
`InvalidArgument`. `memory_allocator` and `secondary_cache` can only be set
in C++.

## Build and test in this repo

This repo also builds the plugin's code as a standalone library (against a
RocksDB source tree) and runs its test suite. From the repo root, with the
locked RocksDB revision:

```bash
cmake -S . -B build -GNinja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DROCKSDB_EXTENSIONS_WITH_NIMBLE=OFF \
  -DROCKSDB_EXTENSIONS_BUILD_TESTS=ON \
  -DROCKSDB_EXTENSIONS_FETCH_DEPS=ON \
  -DROCKSDB_EXTENSIONS_ROCKSDB_GIT_TAG=abeebd9630f11bd08c28b7bd43c7bdfc62050654
cmake --build build --target rocksdb_extensions_fifo_cache_test
ctest --test-dir build -R fifo_cache --output-on-failure
```

`-DROCKSDB_EXTENSIONS_WITH_NIMBLE=OFF` skips the Nimble extension and its
dependencies. To use a local RocksDB checkout instead of fetching, pass
`-DROCKSDB_EXTENSIONS_ROCKSDB_SOURCE_DIR=/path/to/rocksdb` (without
`ROCKSDB_EXTENSIONS_FETCH_DEPS`, GTest must then be installed). Link
`rocksdb_extensions_fifo_cache`.

`scripts/build-fifo-plugin.sh` builds RocksDB itself with the plugin
(Debug, warnings as errors), runs the plugin test suite, and smoke-tests
`db_bench --cache_uri`; CI runs it on every compiler job.

## Tests

`tests/FIFOCacheTest.cc` covers the eviction policy, the lock-free lookup
path and its memory reclamation, capacity and table accounting, pinning,
secondary-cache wrapping, the option string and thread exit. Tests that
steer interleavings with sync points run only in builds without `NDEBUG`
(119 tests optimized, 128 in debug).

## Limitations

- A nested `block_cache={fifo_cache://...}` inside an options string is not
  supported: trimming the braces needs a parsing branch in RocksDB core
  that a plugin cannot add. Set `block_cache` in C++, or pass the bare
  `fifo_cache://` URI to `Cache::CreateFromString` (as `--cache_uri`
  does) with an empty target.
- Braced or space-padded URIs (`"{ fifo_cache://...}"`) do not reach the
  factory, for the same reason. Use the bare form.
- The `fifo_cache://` target must start empty: with a cache already set,
  RocksDB parses the value as options for that cache's own type first.
- Pinned to the RocksDB revision in `dependencies.lock`; newer or older
  checkouts may need adjustments where internal headers changed.
- Release builds need `-DUSE_RTTI=1` (see Requirements).

## Layout

- `include/rocksdb_extensions/fifo_cache/FIFOCache.h`: public API
  (`FIFOCacheOptions`, `RegisterFIFOCache`). FIFOCache's own code.
- `src/FIFOCache.cc`, `src/FIFOCacheImpl.h`: the cache. FIFOCache's own
  code; includes RocksDB's own internal headers.
- `src/SlotTableMapping.h`: the `MAP_NORESERVE` slot-table mapping the
  pinned release lacks. New code the plugin needs (see there).
- `tests/FIFOCacheTest.cc`: the test suite.
- `fifo_cache.mk`, `CMakeLists.txt`, `tests/CMakeLists.txt`,
  `.clang-format`: build files.
- `README.md`: this file.
