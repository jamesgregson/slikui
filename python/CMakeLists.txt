# The Python bindings: one nanobind extension, no libraries (slikui is
# header-only; internal-linkage stb compiles into the module).
#
# Consumed two ways:
#   * wheel builds — scikit-build-core runs CMake with the repo root as the
#     source dir and this file as a subdirectory (see root CMakeLists.txt).
#   * none by itself — `pip install -e .` from the repo root is the entry point.

# nanobind expects the consumer to locate Python first: Python_INCLUDE_DIRS is
# a directory-scope variable, so it must be set in THIS scope, not inside the
# nanobind subdirectory.
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nanobind
                 ${CMAKE_CURRENT_BINARY_DIR}/third_party/nanobind)

nanobind_add_module(_slikui NB_STATIC src/slikui_py.cpp)
target_include_directories(_slikui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# slikui.CanvasSink (numpy) must match the C++ software sink bit-for-bit
# (test_pyrenderer.py). numpy rounds every float32 op; GCC/Clang fuse a*b+c
# into FMA on arm64 by default, shifting rare pixels by 1 LSB. MSVC does not
# contract unless asked (/fp:contract).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(_slikui PRIVATE -ffp-contract=off)
endif()

# scikit-build-core merges this into the wheel next to the `slikui` package.
install(TARGETS _slikui
        LIBRARY DESTINATION slikui
        RUNTIME DESTINATION slikui
        ARCHIVE DESTINATION slikui)
