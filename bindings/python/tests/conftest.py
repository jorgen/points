"""Make `import dew` work when pytest is run directly from the repo.

ctest sets PYTHONPATH to the module's build dir; for manual runs, look for a
built module in the cmake-build-* trees.

Also enables faulthandler -- see below.
"""

import faulthandler
import glob
import os
import sys

# A wheel job that hangs must say WHERE, not burn hours and tell us nothing. Two ~2.5h Windows arm64
# jobs were cancelled with logs ending before pytest printed anything, because `pytest -q` buffers
# its dots when stdout is not a tty.
#
# The watchdog itself is pytest's faulthandler plugin (faulthandler_timeout in pyproject.toml), NOT a
# dump_traceback_later call here: conftest is imported after pytest has already replaced fd 2 with
# its capture pipe, so a hand-rolled watchdog fires but its traceback lands in the capture buffer and
# dies with the process -- observed exactly once, costing a 612s run that named the test and printed
# no stacks. The plugin writes to the real stderr and re-arms per test.
#
# faulthandler.enable() is still worth having for the other half: it catches a hard crash (SIGSEGV,
# SIGABRT) rather than a hang, and vio's thread pool aborts bare on enqueue-after-stop.
faulthandler.enable()

try:
    import dew  # noqa: F401
except ImportError:
    repo = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    for candidate in sorted(glob.glob(os.path.join(repo, "cmake-build-*", "bindings", "python"))):
        # the build tree mirrors the wheel: <candidate>/dew/{__init__.py,_dew.*}
        package = os.path.join(candidate, "dew")
        if os.path.isfile(os.path.join(package, "__init__.py")) and (
            glob.glob(os.path.join(package, "_dew*.so")) or glob.glob(os.path.join(package, "_dew*.pyd"))
        ):
            sys.path.insert(0, candidate)
            break
