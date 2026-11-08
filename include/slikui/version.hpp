// slikui/version.hpp — release version of the library.
//
// THE single source of truth for the version, C++ and Python alike. To
// release, bump the three numbers below — nothing else. Everything derives
// from them:
//   - SLIKUI_VERSION_STRING / slikui::version   (below, via the preprocessor)
//   - CMake project(VERSION)                    (CMakeLists.txt parses them)
//   - PyPI metadata of slikui-python            (pyproject.toml regex provider)
//   - slikui.__version__ in Python              (the compiled extension)
//   - the tag check in the release workflow     (.github/workflows/python.yml)
// Keep the three #define lines in this exact form: those parsers rely on it.
#pragma once

#define SLIKUI_VERSION_MAJOR 0
#define SLIKUI_VERSION_MINOR 3
#define SLIKUI_VERSION_PATCH 0

#define SLIKUI_VERSION_STR_(x) #x
#define SLIKUI_VERSION_XSTR_(x) SLIKUI_VERSION_STR_(x)
#define SLIKUI_VERSION_STRING                  \
    SLIKUI_VERSION_XSTR_(SLIKUI_VERSION_MAJOR) "." \
    SLIKUI_VERSION_XSTR_(SLIKUI_VERSION_MINOR) "." \
    SLIKUI_VERSION_XSTR_(SLIKUI_VERSION_PATCH)
