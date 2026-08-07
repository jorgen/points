"""Make `import dew` work when pytest is run directly from the repo.

ctest sets PYTHONPATH to the module's build dir; for manual runs, look for a
built module in the cmake-build-* trees.

Also installs a hang watchdog -- see below.
"""

import faulthandler
import glob
import os
import sys

# A wheel job that hangs must say WHERE, not burn six hours and tell us nothing.
#
# This has cost real time twice on the Windows arm64 runner: two ~2.5h jobs cancelled with a log
# ending at the last line before pytest started, because `pytest -q` buffers its dots when stdout is
# not a tty, so a hang is indistinguishable from silence.
#
# dump_traceback_later runs its timer on a **C thread**, which is the whole point: the failure modes
# worth catching here are blocking C calls (an unbounded wait inside the library, a callback waiting
# on a loop that never runs), and during one of those the GIL is held, so anything driven by a Python
# thread -- including asyncio timeouts -- never gets to run. A pure-Python watchdog would stay silent
# for exactly the bugs this is here to catch.
#
# exit=True makes the process die after dumping, so the job fails fast with every thread's stack in
# the log instead of hitting the runner's job timeout.
_TIMEOUT = float(os.environ.get("DEW_TEST_WATCHDOG_SECONDS", "600"))
if _TIMEOUT > 0:
    faulthandler.enable()
    faulthandler.dump_traceback_later(_TIMEOUT, exit=True)

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
