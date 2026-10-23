#!/usr/bin/env bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
# This source code is licensed under both the GPLv2 (found in the
# COPYING file in the root directory) and Apache 2.0 License
# (found in the LICENSE.Apache file in the root directory).

# Builds RocksDB at the locked revision with FIFOCache as a plugin, runs the
# plugin's test suite, and smoke-tests db_bench --cache_uri (using the cache
# without application code changes).
#
# Reuses the RocksDB checkout from build-latest-releases.sh when present;
# otherwise clones the locked revision. Honors CC, CXX, CMAKE_GENERATOR,
# BUILD_JOBS, and ROCKSDB_SRC (override for the RocksDB source tree).

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
REPOSITORY_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly REPOSITORY_ROOT

source "${REPOSITORY_ROOT}/dependencies.lock"
if [[ ! "${LOCKED_ROCKSDB_REVISION:-}" =~ ^[0-9a-fA-F]{40}$ ]]; then
  echo "dependencies.lock must define an exact LOCKED_ROCKSDB_REVISION." >&2
  exit 1
fi

build_directory="${FIFO_PLUGIN_BUILD_DIRECTORY:-${REPOSITORY_ROOT}/build-fifo-plugin}"
build_type="${FIFO_PLUGIN_BUILD_TYPE:-Debug}"

rocksdb_src="${ROCKSDB_SRC:-}"
if [[ -z "${rocksdb_src}" ]]; then
  reused="${REPOSITORY_ROOT}/build-latest-releases/_deps/rocksdb-src"
  if [[ -f "${reused}/CMakeLists.txt" && -f "${reused}/cache/sharded_cache.h" ]]; then
    rocksdb_src="${reused}"
  else
    rocksdb_src="${build_directory}/rocksdb-src"
  fi
fi

if [[ ! -f "${rocksdb_src}/CMakeLists.txt" ]]; then
  echo "Cloning RocksDB ${LOCKED_ROCKSDB_REVISION} into ${rocksdb_src}."
  mkdir -p "$(dirname "${rocksdb_src}")"
  git clone --no-checkout https://github.com/facebook/rocksdb.git "${rocksdb_src}"
  git -C "${rocksdb_src}" fetch --depth 1 origin "${LOCKED_ROCKSDB_REVISION}"
  git -C "${rocksdb_src}" checkout "${LOCKED_ROCKSDB_REVISION}"
else
  echo "Using RocksDB source tree: ${rocksdb_src}"
fi

mkdir -p "${rocksdb_src}/plugin"
ln -sfn "${REPOSITORY_ROOT}/fifo_cache" "${rocksdb_src}/plugin/fifo_cache"
echo "Linked plugin: ${rocksdb_src}/plugin/fifo_cache"

cmake_generator_args=()
if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
  cmake_generator_args=(-G "${CMAKE_GENERATOR}")
elif command -v ninja >/dev/null 2>&1; then
  cmake_generator_args=(-G Ninja)
else
  cmake_generator_args=(-G "Unix Makefiles")
fi

build_jobs="${BUILD_JOBS:-}"
if [[ -z "${build_jobs}" ]]; then
  build_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2\n')"
fi

cmake_compiler_launcher_args=()
if command -v ccache >/dev/null 2>&1; then
  launcher="$(command -v ccache)"
  cmake_compiler_launcher_args=(
    -DCMAKE_C_COMPILER_LAUNCHER:FILEPATH="${launcher}"
    -DCMAKE_CXX_COMPILER_LAUNCHER:FILEPATH="${launcher}"
  )
  echo "Compiler launcher: ${launcher}"
fi

plugin_build_dir="${build_directory}/rocksdb-build"
echo "Plugin build directory: ${plugin_build_dir}"

cmake \
  -S "${rocksdb_src}" \
  -B "${plugin_build_dir}" \
  "${cmake_generator_args[@]}" \
  "${cmake_compiler_launcher_args[@]}" \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DROCKSDB_PLUGINS=fifo_cache \
  -DWITH_TESTS=ON \
  -DWITH_ALL_TESTS=ON \
  -DWITH_BENCHMARK_TOOLS=ON \
  -DFAIL_ON_WARNINGS=ON

cmake \
  --build "${plugin_build_dir}" \
  --target FIFOCacheTest db_bench \
  --parallel "${build_jobs}"

"${plugin_build_dir}/FIFOCacheTest"

smoke_db="${build_directory}/db-bench-smoke"
rm -rf "${smoke_db}"
"${plugin_build_dir}/db_bench" \
  --benchmarks=fillseq --num=10000 --value_size=1000 \
  --db="${smoke_db}" --compression_type=none --threads=1 --seed=12345
"${plugin_build_dir}/db_bench" \
  --benchmarks=readrandom --num=10000 --value_size=1000 \
  --db="${smoke_db}" --use_existing_db --compression_type=none \
  --threads=1 --seed=12345 --duration=3 \
  --cache_uri='fifo_cache://capacity=67108864;num_shard_bits=4'

echo "FIFOCache plugin build, tests, and db_bench smoke test passed."
