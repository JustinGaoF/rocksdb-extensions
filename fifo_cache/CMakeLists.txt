# Copyright (c) Meta Platforms, Inc. and affiliates.
# This source code is licensed under both the GPLv2 (found in the
# COPYING file in the root directory) and Apache 2.0 License
# (found in the LICENSE.Apache file in the root directory).

# When added as a RocksDB plugin (ROCKSDB_PLUGINS=fifo_cache), RocksDB's own
# build compiles the sources listed in fifo_cache.mk into librocksdb; this
# file only names the plugin's tests for that build. The SOURCES stay in the
# .mk (shared with the Makefile build) so CMake does not compile them twice.
if(PROJECT_NAME STREQUAL "rocksdb")
  set(fifo_cache_TESTS tests/FIFOCacheTest.cc PARENT_SCOPE)
  return()
endif()

find_package(Threads REQUIRED)

# The plugin's sources include RocksDB's own internal headers, so this needs
# a RocksDB source tree: either ROCKSDB_EXTENSIONS_ROCKSDB_SOURCE_DIR or the
# FetchContent checkout (ROCKSDB_EXTENSIONS_FETCH_DEPS=ON). An installed
# RocksDB package alone is not enough.
if(ROCKSDB_EXTENSIONS_ROCKSDB_SOURCE_DIR)
  set(_fifo_rocksdb_src "${ROCKSDB_EXTENSIONS_ROCKSDB_SOURCE_DIR}")
elseif(DEFINED rocksdb_SOURCE_DIR)
  set(_fifo_rocksdb_src "${rocksdb_SOURCE_DIR}")
else()
  message(FATAL_ERROR
    "FIFOCache needs a RocksDB source tree for its internal headers. Set "
    "ROCKSDB_EXTENSIONS_ROCKSDB_SOURCE_DIR or enable "
    "ROCKSDB_EXTENSIONS_FETCH_DEPS.")
endif()
if(NOT EXISTS "${_fifo_rocksdb_src}/cache/sharded_cache.h")
  message(FATAL_ERROR
    "FIFOCache needs a RocksDB source tree, but ${_fifo_rocksdb_src} has no "
    "cache/sharded_cache.h.")
endif()
set(ROCKSDB_EXTENSIONS_FIFO_ROCKSDB_SRC "${_fifo_rocksdb_src}" CACHE PATH
  "RocksDB source tree used for FIFOCache's internal headers")

# Keep this list in sync with fifo_cache_SOURCES in fifo_cache.mk.
add_library(
  rocksdb_extensions_fifo_cache
  src/FIFOCache.cc)

target_include_directories(
  rocksdb_extensions_fifo_cache
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${_fifo_rocksdb_src})

target_link_libraries(
  rocksdb_extensions_fifo_cache
  PUBLIC
    ${ROCKSDB_EXTENSIONS_ROCKSDB_TARGET}
    Threads::Threads)

# The platform macros RocksDB's own build defines (directory-scoped there,
# so repeated here for this directory and the tests below): port/port.h
# selects its implementation from them.
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  add_definitions(-DOS_LINUX -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX)
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
  add_definitions(-DWIN32 -DOS_WIN -D_MBCS -DWIN64 -DNOMINMAX)
elseif(NOT WIN32)
  add_definitions(-DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX)
endif()

# The warnings RocksDB builds with, as errors, so the plugin stays
# warning-free the way RocksDB's own FAIL_ON_WARNINGS build requires.
# offsetof on the options struct follows RocksDB's own option tables.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    rocksdb_extensions_fifo_cache
    PRIVATE
      -Wall -Wextra -Wsign-compare -Wshadow -Wno-unused-parameter
      -Woverloaded-virtual -Wnon-virtual-dtor -Wno-invalid-offsetof
      -Werror)
endif()

if(ROCKSDB_EXTENSIONS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
