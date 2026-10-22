# Build FIFOCache into RocksDB itself as a RocksDB plugin, so RocksDB's tools
# (e.g. db_bench --cache_uri=fifo_cache://...) can use it: link or copy this
# directory to <rocksdb>/plugin/fifo_cache and build RocksDB with
# ROCKSDB_PLUGINS=fifo_cache (make or CMake).
fifo_cache_SOURCES = src/FIFOCache.cpp src/MemMapping.cpp src/ShardedCache.cpp src/SyncPoint.cpp
fifo_cache_HEADERS = include/rocksdb_extensions/fifo_cache/FIFOCache.h
fifo_cache_FUNC = rocksdb_extensions_fifo_cache_register
