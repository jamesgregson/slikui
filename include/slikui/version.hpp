// slikui/version.hpp — release version of the library.
//
// Single source of truth for the C++ side: CMakeLists.txt parses these
// macros into its project(VERSION), and the Python bindings' lockstep test
// compares them against python/slikui/_version.py. Bump here (and in
// python/slikui/_version.py) for each release.
#pragma once

#define SLIKUI_VERSION_MAJOR 0
#define SLIKUI_VERSION_MINOR 1
#define SLIKUI_VERSION_PATCH 0
#define SLIKUI_VERSION_STRING "0.1.0"
