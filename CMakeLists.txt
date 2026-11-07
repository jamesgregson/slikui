cmake_minimum_required(VERSION 3.20)
project(slikui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(slikui INTERFACE)
target_include_directories(slikui INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(slikui INTERFACE cxx_std_20)

option(SLIKUI_BUILD_TESTS "Build slikui tests" ON)
option(SLIKUI_BUILD_EXAMPLES "Build slikui examples" ON)

if(SLIKUI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SLIKUI_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
