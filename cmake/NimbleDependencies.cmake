# Copyright (c) Meta Platforms, Inc. and affiliates.
# This source code is licensed under both the GPLv2 (found in the
# COPYING file in the root directory) and Apache 2.0 License
# (found in the LICENSE.Apache file in the root directory).

# Dependencies of the Nimble extension, resolved after RocksDB by
# Dependencies.cmake when ROCKSDB_EXTENSIONS_WITH_NIMBLE is ON.

include_guard(GLOBAL)

# Nimble requires both the FlatBuffers headers and flatc. Some system packages
# export incompatible package-name capitalization or omit the compiler. The
# find-package override makes Nimble's find_package(flatbuffers) resolve this
# pinned source build consistently.
if(ROCKSDB_EXTENSIONS_FETCH_DEPS)
  set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY ${ROCKSDB_EXTENSIONS_FLATBUFFERS_GIT_REPOSITORY}
    GIT_TAG ${ROCKSDB_EXTENSIONS_FLATBUFFERS_GIT_TAG}
    GIT_SHALLOW TRUE
    OVERRIDE_FIND_PACKAGE)
  FetchContent_MakeAvailable(flatbuffers)
endif()

if(NOT TARGET fmt::fmt)
  find_package(fmt CONFIG QUIET)
endif()
if(NOT TARGET fmt::fmt)
  find_package(fmt QUIET)
endif()

# These options must be in the cache before Nimble adds its pinned Velox
# submodule. They also apply if Velox has to be resolved separately below.
set(Boost_SOURCE BUNDLED CACHE STRING "" FORCE)
set(FastFloat_SOURCE BUNDLED CACHE STRING "" FORCE)
set(VELOX_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(VELOX_BUILD_TEST_UTILS OFF CACHE BOOL "" FORCE)
set(VELOX_BUILD_MINIMAL_WITH_DWIO ON CACHE BOOL "" FORCE)
set(VELOX_BUILD_RUNNER OFF CACHE BOOL "" FORCE)
set(VELOX_ENABLE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(VELOX_ENABLE_GEO OFF CACHE BOOL "" FORCE)
set(VELOX_ENABLE_PARQUET OFF CACHE BOOL "" FORCE)

find_package(nimble CONFIG QUIET)
set(ROCKSDB_EXTENSIONS_NIMBLE_FETCHED FALSE)
if(NOT TARGET nimble_index_projector AND NOT TARGET nimble::nimble)
  set(NIMBLE_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(NIMBLE_ENABLE_BENCHMARKS OFF CACHE BOOL "" FORCE)
  rocksdb_extensions_add_source_dependency(
    ROCKSDB_EXTENSIONS_NIMBLE_SOURCE_ADDED
    nimble
    "${ROCKSDB_EXTENSIONS_NIMBLE_SOURCE_DIR}")
endif()
if(NOT TARGET nimble_index_projector AND NOT TARGET nimble::nimble AND NOT ROCKSDB_EXTENSIONS_NIMBLE_SOURCE_ADDED)
  rocksdb_extensions_fetch_dependency(
    nimble
    ${ROCKSDB_EXTENSIONS_NIMBLE_GIT_REPOSITORY}
    ${ROCKSDB_EXTENSIONS_NIMBLE_GIT_TAG})
  set(ROCKSDB_EXTENSIONS_NIMBLE_FETCHED TRUE)
endif()

# A source build of Nimble adds the Velox submodule revision that Nimble pins.
# Resolve Velox only after Nimble so CMake does not add an independent Velox
# checkout first and then fail when Nimble creates the same targets again.
if(ROCKSDB_EXTENSIONS_REQUIRE_NIMBLE_PINNED_VELOX AND
   ROCKSDB_EXTENSIONS_NIMBLE_FETCHED AND
   NOT TARGET velox_dwio_common)
  message(FATAL_ERROR
    "The fetched Nimble revision did not provide its pinned Velox targets. "
    "Release validation refuses to fall back to a moving Velox revision; "
    "promote Nimble and Velox as an explicit compatible set instead.")
endif()
find_package(velox CONFIG QUIET)
if(NOT TARGET velox_dwio_common)
  rocksdb_extensions_add_source_dependency(
    ROCKSDB_EXTENSIONS_VELOX_SOURCE_ADDED
    velox
    "${ROCKSDB_EXTENSIONS_VELOX_SOURCE_DIR}")
endif()
if(NOT TARGET velox_dwio_common AND NOT ROCKSDB_EXTENSIONS_VELOX_SOURCE_ADDED)
  rocksdb_extensions_fetch_dependency(
    velox
    ${ROCKSDB_EXTENSIONS_VELOX_GIT_REPOSITORY}
    ${ROCKSDB_EXTENSIONS_VELOX_GIT_TAG})
endif()

# Resolve these after Nimble and Velox so a source build uses their compatible
# pinned revisions. Adding independent source trees first creates duplicate
# Folly::folly and fmt::fmt targets and can mix incompatible versions.
if(NOT TARGET fmt::fmt)
  find_package(fmt CONFIG QUIET)
endif()
if(NOT TARGET fmt::fmt)
  find_package(fmt QUIET)
endif()

if(NOT TARGET Folly::folly AND NOT TARGET folly)
  find_package(Folly CONFIG QUIET)
endif()
if(NOT TARGET Folly::folly AND NOT TARGET folly)
  rocksdb_extensions_add_source_dependency(
    ROCKSDB_EXTENSIONS_FOLLY_SOURCE_ADDED
    folly
    "${ROCKSDB_EXTENSIONS_FOLLY_SOURCE_DIR}")
endif()
if(NOT TARGET Folly::folly AND NOT TARGET folly AND NOT ROCKSDB_EXTENSIONS_FOLLY_SOURCE_ADDED)
  rocksdb_extensions_fetch_dependency(
    folly
    ${ROCKSDB_EXTENSIONS_FOLLY_GIT_REPOSITORY}
    ${ROCKSDB_EXTENSIONS_FOLLY_GIT_TAG})
endif()

rocksdb_extensions_resolve_target(
  ROCKSDB_EXTENSIONS_FOLLY_TARGET
  Folly::folly
  folly)
if(NOT ROCKSDB_EXTENSIONS_FOLLY_TARGET)
  message(FATAL_ERROR
    "Could not find a Folly CMake target. Provide Folly::folly or folly "
    "through CMAKE_PREFIX_PATH, ROCKSDB_EXTENSIONS_FOLLY_SOURCE_DIR, or "
    "ROCKSDB_EXTENSIONS_FETCH_DEPS.")
endif()

if(NOT TARGET fmt::fmt)
  message(FATAL_ERROR
    "Could not find fmt::fmt through CMAKE_PREFIX_PATH or the system "
    "package registry.")
endif()

# Keep these lists to targets the extension's public headers or implementation
# use directly. Transitive dependencies should come from each dependency's own
# CMake targets.
set(ROCKSDB_EXTENSIONS_VELOX_TARGETS
  velox_common_base
  velox_caching
  velox_file
  velox_memory
  velox_type
  velox_dwio_common
  velox_exception
  CACHE STRING "Velox targets required by the Nimble RocksDB extension")

set(ROCKSDB_EXTENSIONS_NIMBLE_TARGETS
  nimble_common
  nimble_deserializer
  nimble_serializer
  nimble_tablet_reader
  nimble_tablet_reader_cache
  nimble_velox_common
  nimble_index_projector
  CACHE STRING "Nimble targets required by the Nimble RocksDB extension")

rocksdb_extensions_require_targets(
  "Velox"
  ${ROCKSDB_EXTENSIONS_VELOX_TARGETS})
rocksdb_extensions_require_targets(
  "Nimble"
  ${ROCKSDB_EXTENSIONS_NIMBLE_TARGETS})
