"""Python bindings for dewfall: convert, inspect and stream point cloud datasets.

The compiled core is ``dew._dew``, generated from the public C headers by
``tools/bindgen`` and re-exported here. This package also carries the pieces a
wheel needs to be useful beyond Python: the shared libraries, the public C
headers, and a CMake package config, reachable through :func:`get_include`,
:func:`get_lib_dir` and :func:`get_cmake_dir`.

    import dew

    conv = dew.Converter("out.dew", dew.ConverterOpenFileSemantics.truncate)
    conv.add_data_file(["input.laz"])
    conv.wait_idle()

See ``examples/python/numpy_to_dew.py`` for feeding point data straight from
numpy arrays.
"""

import os
import sys
from pathlib import Path

__all__ = ["get_include", "get_lib_dir", "get_cmake_dir", "__version__"]

_PACKAGE_DIR = Path(__file__).parent.resolve()

# Windows has no rpath. The dew_*.dll ship beside this package's extension
# module, which Windows searches when loading it, so the import below normally
# just works -- but register the directory explicitly as well, so an
# interpreter or loader with a restricted search path still finds them.
if sys.platform == "win32":  # pragma: no cover - platform specific
    if hasattr(os, "add_dll_directory"):
        for _candidate in (_PACKAGE_DIR, _PACKAGE_DIR / "lib"):
            if _candidate.is_dir():
                os.add_dll_directory(str(_candidate))

from ._dew import *  # noqa: E402,F401,F403  (after the DLL path fix above)
from . import _dew as _ext  # noqa: E402

__all__ += [name for name in dir(_ext) if not name.startswith("_")]

try:
    from importlib.metadata import PackageNotFoundError, version

    __version__ = version("dewfall")
except (ImportError, PackageNotFoundError):  # pragma: no cover - source builds
    __version__ = "0.0.0+unknown"


def get_include() -> str:
    """Directory holding the public C headers, for compiling against dewfall.

    Include them as ``<dew/converter/converter.h>``::

        cc -I"$(python -c 'import dew; print(dew.get_include())')" ...
    """
    return str(_PACKAGE_DIR / "include")


def get_lib_dir() -> str:
    """Directory holding the shared ``dew_core`` / ``dew_render`` / ``dew_converter``."""
    return str(_PACKAGE_DIR / "lib")


def get_cmake_dir() -> str:
    """Directory holding ``dewConfig.cmake``, for ``find_package(dew)``.

    Point CMake at it with::

        cmake -DCMAKE_PREFIX_PATH="$(python -c 'import dew; print(dew.get_cmake_dir())')" ...

    then ``find_package(dew REQUIRED)`` and link ``dew::dew_converter``.
    """
    return str(_PACKAGE_DIR / "cmake")
