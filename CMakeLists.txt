cmake_minimum_required(VERSION 3.16)
project(ClusterCompressionStudy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies found via CMAKE_PREFIX_PATH (set by aliBuild)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(LZ4  REQUIRED IMPORTED_TARGET liblz4)
find_package(ZLIB REQUIRED)
# Brotli: no CMake config, no reliable pkg-config in aliBuild — find manually via BROTLI_ROOT
find_library(BROTLIENC_LIB  NAMES brotlienc  HINTS $ENV{BROTLI_ROOT}/lib)
find_library(BROTLIDEC_LIB  NAMES brotlidec  HINTS $ENV{BROTLI_ROOT}/lib)
find_library(BROTLICOMMON_LIB NAMES brotlicommon HINTS $ENV{BROTLI_ROOT}/lib)
find_path(BROTLI_INCLUDE_DIR brotli/encode.h HINTS $ENV{BROTLI_ROOT}/include)
if(NOT BROTLIENC_LIB OR NOT BROTLIDEC_LIB OR NOT BROTLICOMMON_LIB OR NOT BROTLI_INCLUDE_DIR)
  message(FATAL_ERROR "Brotli not found. Set BROTLI_ROOT or CMAKE_PREFIX_PATH.")
endif()
add_library(brotli INTERFACE)
target_link_libraries(brotli INTERFACE ${BROTLIENC_LIB} ${BROTLIDEC_LIB} ${BROTLICOMMON_LIB})
target_include_directories(brotli INTERFACE ${BROTLI_INCLUDE_DIR})
find_package(Snappy REQUIRED)

# --- Library ---
add_library(cluster_compression
  src/Compressor.cpp
  src/ClusterIO.cpp
  src/Cluster.cpp
)
target_include_directories(cluster_compression PUBLIC include)
target_link_libraries(cluster_compression PUBLIC
  PkgConfig::ZSTD
  PkgConfig::LZ4
  ZLIB::ZLIB
  brotli
  Snappy::snappy
)

# --- Benchmark ---
add_executable(cluster_bench bench/main.cpp)
target_link_libraries(cluster_bench PRIVATE cluster_compression)
