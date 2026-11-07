# Single source of truth for the Python-side version. C++ lives in
# include/slikui/version.hpp — test_version.py enforces the lockstep.
# PyPI metadata reads this file (scikit-build-core regex provider).
__version__ = "0.1.0"
