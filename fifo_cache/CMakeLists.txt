# Copyright (c) Meta Platforms, Inc. and affiliates.
# This source code is licensed under both the GPLv2 (found in the
# COPYING file in the root directory) and Apache 2.0 License
# (found in the LICENSE.Apache file in the root directory).

find_package(Threads REQUIRED)

# Keep this list in sync with fifo_cache_SOURCES in fifo_cache.mk.
add_library(
  rocksdb_extensions_fifo_cache
  src/FIFOCache.cpp
  src/MemMapping.cpp
  src/ShardedCache.cpp
  src/SyncPoint.cpp)

target_include_directories(
  rocksdb_extensions_fifo_cache
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(
  rocksdb_extensions_fifo_cache
  PUBLIC
    ${ROCKSDB_EXTENSIONS_ROCKSDB_TARGET}
    Threads::Threads)

# The warnings RocksDB builds with, so building this directory as a RocksDB
# plugin (fifo_cache.mk) sees nothing new. offsetof on the options struct
# follows RocksDB's own option tables.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    rocksdb_extensions_fifo_cache
    PRIVATE
      -Wall -Wextra -Wsign-compare -Wshadow -Wno-unused-parameter
      -Woverloaded-virtual -Wnon-virtual-dtor -Wno-invalid-offsetof)
endif()

if(ROCKSDB_EXTENSIONS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
