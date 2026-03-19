cmake_minimum_required(VERSION 3.31)
project(AOIDL LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)

# Force utf-8 on all source files
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

include(FetchContent)

FetchContent_Declare(lexy URL https://lexy.foonathan.net/download/lexy-src.zip)
FetchContent_MakeAvailable(lexy)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.12.0 # Use the latest stable version
)
FetchContent_MakeAvailable(Catch2)

set(BOOST_INCLUDE_LIBRARIES container_hash program_options)
set(BOOST_ENABLE_CMAKE ON)

FetchContent_Declare(
  Boost
  URL https://github.com/boostorg/boost/releases/download/boost-1.90.0/boost-1.90.0-cmake.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)

FetchContent_MakeAvailable(Boost)

FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  blake3
  GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
  GIT_TAG        1.8.3
  SOURCE_SUBDIR  c
)

FetchContent_MakeAvailable(blake3)

# Packing helpers
add_subdirectory(pack)
add_subdirectory(meta)
add_subdirectory(compiler)
add_subdirectory(cli)
