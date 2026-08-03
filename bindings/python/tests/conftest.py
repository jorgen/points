"""Make `import dew` work when pytest is run directly from the repo.

ctest sets PYTHONPATH to the module's build dir; for manual runs, look for a
built module in the cmake-build-* trees.
"""

import glob
import os
import sys

try:
    import dew  # noqa: F401
except ImportError:
    repo = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    for candidate in sorted(glob.glob(os.path.join(repo, "cmake-build-*", "bindings", "python"))):
        if glob.glob(os.path.join(candidate, "dew.*.so")) or glob.glob(os.path.join(candidate, "dew.pyd")):
            sys.path.insert(0, candidate)
            break
