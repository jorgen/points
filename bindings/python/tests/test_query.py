"""Reading side of the bindings: Dataset.query_box.

query_box is hand-written (bindings/python/custom/query.h) rather than generated, because the C
request lifecycle -- submit, wait, borrow buffers, release -- is not a shape Python wants. These
tests pin the two properties that hand-written layer is responsible for:

* the arrays Python gets back are COPIES it owns. The C result borrows request-owned memory that
  dies at dew_request_release, so a view would dangle the moment query_box returned;

* a full-resolution query returns the SOURCE point count. Interior LOD nodes are subsampled copies
  of their descendants, so a walk that emitted every level would inflate the count 2-3x while every
  individual point still looked valid.
"""

import gc

import numpy as np
import pytest

import dew

from test_smoke import _install_synthetic_callbacks  # noqa: F401  (shared converter callbacks)

TOTAL = 4096


@pytest.fixture()
def dataset_path(tmp_path):
    path = str(tmp_path / "query.dew")
    converter = dew.Converter(path, dew.ConverterOpenFileSemantics.truncate)
    _install_synthetic_callbacks(converter)
    # Force subdivision: with everything in one node there would be no interior LOD nodes, and the
    # point-count assertion below would hold trivially.
    converter.set_node_point_limit(256)
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert converter.status() == dew.ConverterConversionStatus.completed
    del converter
    return path


def test_open_and_describe(dataset_path):
    ds = dew.open_dataset(dataset_path)
    assert ds.state() == dew.DatasetState.ready

    info = ds.get_info()
    assert info.scale > 0
    # aabb_* is the root octree cell, so it is a genuine box (min <= max) and contains every point.
    for lo, hi in zip(info.aabb_min, info.aabb_max):
        assert hi >= lo

    names = [ds.get_attribute_name(i) for i in range(ds.attribute_count())]
    assert "xyz" in names


def test_open_missing_dataset_raises():
    with pytest.raises(RuntimeError):
        dew.open_dataset("definitely-not-a-dataset.dew")


def test_query_whole_dataset_returns_every_point(dataset_path):
    ds = dew.open_dataset(dataset_path)
    info = ds.get_info()
    pad = 1.0 + max(hi - lo for lo, hi in zip(info.aabb_min, info.aabb_max))
    lo = [v - pad for v in info.aabb_min]
    hi = [v + pad for v in info.aabb_max]

    result = ds.query_box(lo, hi, lod="full", clip_points=False)
    assert result["point_count"] == TOTAL
    assert result["node_count"] >= 1
    assert result["xyz"].shape == (TOTAL, 3)
    assert result["xyz"].dtype == np.float64


def test_sub_box_is_a_strict_subset(dataset_path):
    ds = dew.open_dataset(dataset_path)
    # Derive the box from where the points actually ARE: the reported bounds are the octree cell,
    # which can be much larger than the data, so its middle can be empty.
    info = ds.get_info()
    probe = ds.query_box(list(info.aabb_min), list(info.aabb_max), lod="full", clip_points=False)
    lo = probe["xyz"].min(axis=0)
    hi = probe["xyz"].max(axis=0)
    centre = (lo + hi) / 2
    quarter = np.maximum((hi - lo) / 4, 1e-6)

    box_min = list(centre - quarter)
    box_max = list(centre + quarter)
    clipped = ds.query_box(box_min, box_max, lod="full", clip_points=True)

    assert 0 < clipped["point_count"] < TOTAL
    xyz = clipped["xyz"]
    # Point clipping means EXACTLY inside, so nothing may fall outside the box.
    assert np.all(xyz >= np.array(box_min) - 1e-9)
    assert np.all(xyz <= np.array(box_max) + 1e-9)

    # Node clipping selects whole octree cells, so it can only ever return more.
    whole_nodes = ds.query_box(box_min, box_max, lod="full", clip_points=False)
    assert whole_nodes["point_count"] >= clipped["point_count"]


def test_returned_arrays_survive_the_dataset(dataset_path):
    """The arrays are copies, not views into request memory."""
    ds = dew.open_dataset(dataset_path)
    info = ds.get_info()
    pad = 1.0 + max(hi - lo for lo, hi in zip(info.aabb_min, info.aabb_max))
    result = ds.query_box([v - pad for v in info.aabb_min], [v + pad for v in info.aabb_max], clip_points=False)

    xyz = result["xyz"]
    expected = xyz.copy()

    # Drop the dataset (and with it the reader, caches and any request memory), then force a
    # collection. A borrowed view would be reading freed memory here.
    del ds, result
    gc.collect()

    # The values survive, which is the property that matters. owndata is False because the buffer is
    # kept alive by a nanobind capsule rather than by numpy itself -- still Python-managed, still
    # freed by the interpreter, just not numpy's own allocation.
    assert np.array_equal(xyz, expected)


def test_lod_modes_return_a_subsample_not_a_superset(dataset_path):
    ds = dew.open_dataset(dataset_path)
    info = ds.get_info()
    pad = 1.0 + max(hi - lo for lo, hi in zip(info.aabb_min, info.aabb_max))
    lo = [v - pad for v in info.aabb_min]
    hi = [v + pad for v in info.aabb_max]

    full = ds.query_box(lo, hi, lod="full", clip_points=False)
    budget = ds.query_box(lo, hi, lod="budget", max_points=512, clip_points=False)

    # A coarser level of detail must never return MORE points than full resolution -- that is exactly
    # the double-counting failure mode, and it would look like a plausible result.
    assert budget["point_count"] <= full["point_count"]
    assert budget["point_count"] > 0


def test_position_formats(dataset_path):
    ds = dew.open_dataset(dataset_path)
    info = ds.get_info()
    pad = 1.0 + max(hi - lo for lo, hi in zip(info.aabb_min, info.aabb_max))
    lo = [v - pad for v in info.aabb_min]
    hi = [v + pad for v in info.aabb_max]

    assert ds.query_box(lo, hi, position_format="r64", clip_points=False)["xyz"].dtype == np.float64
    assert ds.query_box(lo, hi, position_format="r32", clip_points=False)["xyz"].dtype == np.float32
    assert ds.query_box(lo, hi, position_format="i32", clip_points=False)["xyz"].dtype == np.int32

    with pytest.raises(ValueError):
        ds.query_box(lo, hi, position_format="nope")
    with pytest.raises(ValueError):
        ds.query_box(lo, hi, lod="nope")
    with pytest.raises(ValueError):
        ds.query_box([0.0, 0.0], hi)
