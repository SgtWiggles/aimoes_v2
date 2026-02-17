cmake_minimum_required(VERSION 3.31)
project(AOIDL CXX)
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

set(BOOST_INCLUDE_LIBRARIES container_hash)
set(BOOST_ENABLE_CMAKE ON)

include(FetchContent)
FetchContent_Declare(
  Boost
  GIT_REPOSITORY https://github.com/boostorg/boost.git
  GIT_TAG boost-1.90.0
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(Boost)


# Packing helpers
add_subdirectory(pack)
add_subdirectory(compiler)
