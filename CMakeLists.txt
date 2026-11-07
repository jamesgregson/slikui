cmake_minimum_required(VERSION 3.20)

# Version comes from include/slikui/version.hpp — the single source of truth
# for C++ and Python alike.
set(_slikui_version "")
foreach(_part MAJOR MINOR PATCH)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/slikui/version.hpp" _line
       REGEX "^#define SLIKUI_VERSION_${_part} [0-9]+$")
  if(NOT _line)
    message(FATAL_ERROR "SLIKUI_VERSION_${_part} not found in include/slikui/version.hpp")
  endif()
  string(REGEX REPLACE "^#define SLIKUI_VERSION_${_part} ([0-9]+)$" "\\1" _num "${_line}")
  list(APPEND _slikui_version "${_num}")
endforeach()
list(JOIN _slikui_version "." _slikui_version)

project(slikui VERSION "${_slikui_version}" LANGUAGES CXX)
message(STATUS "slikui ${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(slikui INTERFACE)
target_include_directories(slikui INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(slikui INTERFACE cxx_std_20)

option(SLIKUI_BUILD_TESTS "Build slikui tests" ON)
option(SLIKUI_BUILD_EXAMPLES "Build slikui examples" ON)

if(SKBUILD)
  # Python wheel build (scikit-build-core): only the bindings, which install
  # into the wheel — the install/export rules below don't apply.
  add_subdirectory(python)
else()
  if(SLIKUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()

  if(SLIKUI_BUILD_EXAMPLES)
    add_subdirectory(examples)
  endif()

  # --- install / export: FetchContent / find_package(slikui) support ---
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS slikui EXPORT slikuiTargets)
  install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

  set(SLIKUI_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/slikui")
  install(EXPORT slikuiTargets
          FILE slikuiTargets.cmake
          NAMESPACE slikui::
          DESTINATION "${SLIKUI_INSTALL_CMAKEDIR}")

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/slikuiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/slikuiConfig.cmake"
    INSTALL_DESTINATION "${SLIKUI_INSTALL_CMAKEDIR}")
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/slikuiConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
  install(FILES
          "${CMAKE_CURRENT_BINARY_DIR}/slikuiConfig.cmake"
          "${CMAKE_CURRENT_BINARY_DIR}/slikuiConfigVersion.cmake"
          DESTINATION "${SLIKUI_INSTALL_CMAKEDIR}")
endif()
