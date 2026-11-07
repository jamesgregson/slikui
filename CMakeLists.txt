cmake_minimum_required(VERSION 3.20)
project(silk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(silk INTERFACE)
target_include_directories(silk INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(silk INTERFACE cxx_std_20)

option(SILK_BUILD_TESTS "Build silk tests" ON)
option(SILK_BUILD_EXAMPLES "Build silk examples" ON)

if(SILK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SILK_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
