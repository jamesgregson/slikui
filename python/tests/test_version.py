"""Version: include/slikui/version.hpp is the single source of truth.

slikui.__version__ comes from the compiled extension (SLIKUI_VERSION_STRING),
and the PyPI metadata of slikui-python comes from the same header through the
pyproject regex provider. These tests check the two paths agree, so a broken
regex or a stale build shows up here rather than on PyPI.
"""

import re
from importlib import metadata

import pytest

import slikui
import slikui._slikui


def test_version_is_semver_triple():
    assert re.fullmatch(r"\d+\.\d+\.\d+", slikui.__version__)


def test_python_version_is_extension_version():
    assert slikui.__version__ == slikui._slikui.__version__


def test_distribution_metadata_matches_header():
    try:
        dist_version = metadata.version("slikui-python")
    except metadata.PackageNotFoundError:
        pytest.skip("slikui-python not installed (running from a bare build)")
    assert dist_version == slikui.__version__
