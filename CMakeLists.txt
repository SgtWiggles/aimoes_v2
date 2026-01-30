cmake_minimum_required(VERSION 3.15)
project(AOIDL CXX)
set(CMAKE_CXX_STANDARD 23)

include(FetchContent)

FetchContent_Declare(lexy URL https://lexy.foonathan.net/download/lexy-src.zip)
FetchContent_MakeAvailable(lexy)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.12.0 # Use the latest stable version
)
FetchContent_MakeAvailable(Catch2)

# Packing helpers
add_subdirectory(pack)
add_subdirectory(compiler)
