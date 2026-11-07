"""Test support: import python/examples/glfw_demo.py as a module.

The examples dir is not a package; this loader keeps the demo importable for
headless tests without touching its run-as-script behavior.
"""

import importlib.util
from pathlib import Path

_PATH = Path(__file__).resolve().parents[1] / "examples" / "glfw_demo.py"


def load_demo():
    spec = importlib.util.spec_from_file_location("glfw_demo", _PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod
