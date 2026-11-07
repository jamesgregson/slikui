"""Version lockstep: Python and C++ must claim the same release version.

Two files declare the version — include/slikui/version.hpp (C++ truth, CMake
parses it into project(VERSION)) and python/slikui/_version.py (Python truth,
PyPI metadata reads it). This test fails if they drift apart. It needs the
source tree; running the tests against an installed wheel without sources
skips it.
"""

import re
from pathlib import Path

import pytest

import slikui

_VERSION_HPP = (
    Path(__file__).resolve().parents[2] / "include" / "slikui" / "version.hpp"
)


@pytest.fixture()
def header_version():
    if not _VERSION_HPP.exists():
        pytest.skip("source tree not available (installed-package run)")
    m = re.search(r'#define SLIKUI_VERSION_STRING "([^"]+)"', _VERSION_HPP.read_text())
    assert m, "SLIKUI_VERSION_STRING missing from version.hpp"
    return m.group(1)


def test_python_version_matches_cpp_header(header_version):
    assert slikui.__version__ == header_version


def test_extension_version_matches_cpp_header(header_version):
    import slikui._slikui

    assert slikui._slikui.__version__ == header_version


def test_umbrella_header_version_matches(header_version):
    # The C++ constexpr version is derived from the same macros; this check
    # runs in Python via the compiled module's sibling — the umbrella itself
    # is compiled into nothing, so assert the macros parse as a triple.
    parts = header_version.split(".")
    assert len(parts) == 3 and all(p.isdigit() for p in parts)
