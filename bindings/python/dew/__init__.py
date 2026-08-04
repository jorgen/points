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

Reading is request-based: open a dataset and ask for a region.

    ds = dew.open_dataset("scan.dew")
    result = ds.query_box([0, 0, 0], [10, 10, 10], attributes=["intensity"])
    xyz = result["xyz"]              # (N, 3) float64
    intensity = result["intensity"]  # (N,)   uint16

See ``examples/python/`` for runnable scripts: ``numpy_to_dew.py`` feeds point
data straight from numpy arrays, and ``query_box.py`` converts a dataset,
queries a sub-box and renders it.
"""

import os
import sys
from pathlib import Path

__all__ = ["open_dataset", "get_include", "get_lib_dir", "get_cmake_dir", "__version__"]

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


def open_dataset(url, connection: str = "", *, memory_budget_bytes: int = 0, decode_threads: int = 0, max_reads_in_flight: int = 0):
    """Open a converted ``.dew`` dataset for reading.

    A thin wrapper over :class:`Dataset` that fills in the options struct, so the common case is
    one argument::

        ds = dew.open_dataset("scan.dew")

    ``connection`` carries cloud credentials for ``s3://`` / ``az://`` URLs (same grammar as the
    converter's); leave it empty for local paths or when credentials come from the environment.

    Raises ``RuntimeError`` if the dataset cannot be opened -- a missing or corrupt dataset is
    reported through the handle's state rather than by the constructor, so this checks it for you.
    """
    options = DatasetOptions()  # noqa: F405
    options.memory_budget_bytes = memory_budget_bytes
    options.decode_threads = decode_threads
    options.max_reads_in_flight = max_reads_in_flight
    dataset = Dataset(str(url), connection, options)  # noqa: F405
    if dataset.state() != DatasetState.ready:  # noqa: F405
        raise RuntimeError(f"could not open dataset {url!r}")
    return dataset
