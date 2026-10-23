# Copyright (c) Meta Platforms, Inc. and affiliates.
# This source code is licensed under both the GPLv2 (found in the
# COPYING file in the root directory) and Apache 2.0 License
# (found in the LICENSE.Apache file in the root directory).

find_package(GTest CONFIG QUIET)
if(NOT TARGET GTest::gtest AND ROCKSDB_EXTENSIONS_FETCH_DEPS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
if(NOT TARGET GTest::gtest)
  message(FATAL_ERROR
    "Could not find GTest::gtest. Provide a GTest package or enable "
    "ROCKSDB_EXTENSIONS_FETCH_DEPS.")
endif()

# ASSERT_OK in the test calls into test_util/testharness.cc, which RocksDB's
# own build puts in its testharness library; here that file is compiled from
# the RocksDB source tree straight into the test.
add_executable(
  rocksdb_extensions_fifo_cache_test
  FIFOCacheTest.cc
  ${ROCKSDB_EXTENSIONS_FIFO_ROCKSDB_SRC}/test_util/testharness.cc)

# The test includes RocksDB's internal headers (test helpers, port) from the
# same source tree as the library.
target_include_directories(
  rocksdb_extensions_fifo_cache_test
  PRIVATE
    ${ROCKSDB_EXTENSIONS_FIFO_ROCKSDB_SRC})

target_link_libraries(
  rocksdb_extensions_fifo_cache_test
  PRIVATE
    rocksdb_extensions_fifo_cache
    GTest::gtest)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    rocksdb_extensions_fifo_cache_test
    PRIVATE
      -Wall -Wextra -Wsign-compare -Wshadow -Wno-unused-parameter
      -Woverloaded-virtual -Wnon-virtual-dtor -Wno-invalid-offsetof
      -Werror)
endif()

add_test(NAME rocksdb_extensions_fifo_cache_test
         COMMAND rocksdb_extensions_fifo_cache_test)
