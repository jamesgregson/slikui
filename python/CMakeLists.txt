cmake_minimum_required(VERSION 3.20)
project(silk-python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Vendored nanobind (repo policy: vendored like stb, no network at build time).
# nanobind expects the consumer to locate Python first: Python_INCLUDE_DIRS is
# a directory-scope variable, so it must be set in THIS scope, not inside the
# nanobind subdirectory.
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nanobind
                 ${CMAKE_CURRENT_BINARY_DIR}/third_party/nanobind)

# The whole library is header-only, so the extension is a few TUs and no
# libraries. _silk compiles all of silk (incl. internal-linkage stb).
nanobind_add_module(_silk NB_STATIC src/silk_py.cpp)
target_include_directories(_silk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# scikit-build-core merges this into the wheel next to the `silk` package.
install(TARGETS _silk
        LIBRARY DESTINATION silk
        RUNTIME DESTINATION silk
        ARCHIVE DESTINATION silk)
