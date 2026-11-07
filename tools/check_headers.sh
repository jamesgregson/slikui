#!/bin/sh
# Compile every public header standalone — catches missing std includes.
#
# apple-libc++ pulls <cstdint>/<functional>/<string> transitively from
# unrelated headers; libstdc++ (Linux CI) and MSVC don't, so a header that
# uses std::uint8_t without #include <cstdint> compiles on a Mac and fails
# everywhere else. Run in CI with CXX=g++ (libstdc++) for exactly that
# reason. Override the compiler via CXX, extra flags via CXXFLAGS.
set -e
cd "$(dirname "$0")/.."

status=0
for h in $(find include -name '*.hpp' | sort); do
    if ! ${CXX:-c++} -std=c++20 ${CXXFLAGS:-} -Iinclude -fsyntax-only "$h" 2>/tmp/slikui_check_headers.err; then
        echo "FAIL $h"
        sed -n '1,6p' /tmp/slikui_check_headers.err
        status=1
    fi
done
[ "$status" -eq 0 ] && echo "all headers compile standalone ($(${CXX:-c++} --version | head -1))"
exit $status
