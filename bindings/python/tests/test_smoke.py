"""End-to-end smoke tests for the generated dew Python bindings.

The producer test is a Python port of the synthetic file callbacks in
tests/private/cached_upload_tests.cpp: pre_init/init/convert_data implemented
in Python, point data written into the library's buffers through numpy views,
on the converter's internal threads. It exercises, in one pass: generated
trampolines, GIL acquisition from foreign threads, GIL release around
blocking calls, ndarray views, error/exception plumbing, and stats structs.
"""

import threading

import numpy as np
import pytest

import dew

TOTAL = 4096


# ---------------------------------------------------------------------------
# introspection
# ---------------------------------------------------------------------------


def test_module_surface():
    for name in (
        "Converter",
        "ConverterDataSource",
        "ConverterHeader",
        "ConverterFilePreInitInfo",
        "Renderer",
        "Camera",
        "Arcball",
        "Fps",
        "Error",
        "Type",
        "Components",
        "ConverterCompression",
        "SkyboxData",
        "DataSource",
    ):
        assert hasattr(dew, name), name
    assert dew.ATTRIBUTE_XYZ == "xyz"
    assert int(dew.ConverterCompression.zstd) == 2
    assert int(dew.ConverterCompression.huff0) == 3
    assert int(dew.Components.components_4x4) == 5


def test_gl_plumbing_is_not_bound():
    assert not hasattr(dew, "Buffer")
    assert not hasattr(dew, "ToRender")
    assert not hasattr(dew.Renderer, "frame")
    assert not hasattr(dew.Renderer, "set_callback")


# ---------------------------------------------------------------------------
# camera math (pure, no GL)
# ---------------------------------------------------------------------------


def test_camera_round_trip():
    camera = dew.Camera()
    camera.look_at([0.0, 0.0, 10.0], [0.0, 0.0, 0.0], [0.0, 1.0, 0.0])
    view = camera.get_view_matrix()
    assert len(view) == 16
    assert view[14] == pytest.approx(-10.0)

    camera.set_view_matrix(view)
    assert camera.get_view_matrix() == pytest.approx(view)

    camera.set_perspective(1.2, 800.0, 600.0, 0.1, 1000.0)
    props = camera.perspective_properties()
    assert props["fov"] == pytest.approx(1.2)
    assert props["aspect"] == pytest.approx(800.0 / 600.0)

    eye = camera.get_eye()
    assert eye == pytest.approx([0.0, 0.0, 10.0])


def test_controllers_keep_camera_alive():
    arcball = dew.Arcball(dew.Camera(), [0.0, 0.0, 0.0])  # camera kept alive by keep_alive
    arcball.rotate(0.1, 0.2, 0.0)
    arcball.pan(0.05, 0.05)
    assert arcball.get_center() == pytest.approx([0.0, 0.0, 0.0], abs=1.0)

    fps = dew.Fps(dew.Camera())
    fps.move(1.0, 0.0, 0.0)


def test_look_at_aabb_struct_arg():
    aabb = dew.Aabb()
    aabb.min = [0.0, 0.0, 0.0]
    aabb.max = [10.0, 10.0, 10.0]
    camera = dew.Camera()
    camera.set_perspective(1.2, 800.0, 600.0, 0.1, 1000.0)
    camera.look_at_aabb(aabb, [0.0, 0.0, -1.0], [0.0, 1.0, 0.0])
    # eye lands at center + direction * distance, outside the box
    assert camera.get_eye()[2] < 0.0


# ---------------------------------------------------------------------------
# errors
# ---------------------------------------------------------------------------


def test_error_exception():
    with pytest.raises(dew.Error) as excinfo:
        dew.Converter("/nonexistent/dir/x.dew", dew.ConverterOpenFileSemantics.open_existing)
    error = excinfo.value
    # .code/.message are attached by the C++ translator. Building the instance
    # through the format-string C-API silently failed under Py_LIMITED_API on
    # CPython 3.12 (but not 3.14), degrading the exception to a bare message --
    # so assert the whole shape, not just that something was raised.
    assert error.code != 0
    assert isinstance(error.code, int)
    assert error.message
    assert isinstance(error.message, str)
    assert str(error) == error.message
    assert error.args == (error.message,)


def test_file_converter_callbacks_require_all_three(tmp_path):
    converter = dew.Converter(str(tmp_path / "x.dew"), dew.ConverterOpenFileSemantics.truncate)
    with pytest.raises(ValueError):
        converter.set_file_converter_callbacks(pre_init=None, init=None, convert_data=lambda *a: (0, True))


# ---------------------------------------------------------------------------
# the Python point producer (numpy views on internal threads)
# ---------------------------------------------------------------------------


def _install_synthetic_callbacks(converter, probe=None):
    def pre_init(filename):
        base = int(filename.rsplit("_", 1)[1])
        info = dew.ConverterFilePreInitInfo()
        info.found_aabb_min = 1
        info.aabb_min = [float(base), 0.0, 0.0]
        info.approximate_point_count = TOTAL
        info.approximate_point_size_bytes = 16
        info.found_point_count = 1
        info.input_file_size_bytes = TOTAL * 16
        return info

    def init(filename, header, attributes):
        base = int(filename.rsplit("_", 1)[1])
        header.point_count = TOTAL
        header.offset = [0.0, 0.0, 0.0]
        header.scale = [0.001, 0.001, 0.001]
        header.min = [base * 0.001, 0.0, 0.0]
        header.max = [(base + TOTAL) * 0.001, 0.001, 0.001]
        attributes.add_attribute(dew.ATTRIBUTE_XYZ, dew.Type.i32, dew.Components.components_3)
        return {"base": base, "total": TOTAL, "produced": 0}

    def convert_data(state, header, attributes, buffers, max_points):
        assert attributes and attributes[0][0] == "xyz"
        xyz = buffers[0]
        assert xyz.dtype == np.int32 and xyz.shape[1] == 3
        # The buffer MUST alias the converter's own memory. nanobind's default
        # return policy hands back a detached copy, which silently discards
        # everything written here -- the dataset would then hold uninitialized
        # heap. owndata=False / base is not None is the guard against that.
        assert not xyz.flags.owndata, "convert_data buffer is a copy, not a view into the library buffer"
        assert xyz.base is not None
        assert xyz.flags.writeable
        if probe is not None:
            probe.setdefault("addresses", []).append(xyz.__array_interface__["data"][0])
        count = min(state["total"] - state["produced"], max_points, xyz.shape[0])
        index = np.arange(count)
        xyz[:count, 0] = state["base"] + state["produced"] + index
        xyz[:count, 1] = (state["produced"] + index) % 7
        xyz[:count, 2] = (state["produced"] + index) % 3
        state["produced"] += count
        return count, state["produced"] == state["total"]

    converter.set_file_converter_callbacks(pre_init=pre_init, init=init, convert_data=convert_data)


@pytest.fixture()
def synthetic_dataset(tmp_path):
    cache = str(tmp_path / "synthetic.dew")
    converter = dew.Converter(cache, dew.ConverterOpenFileSemantics.truncate)
    _install_synthetic_callbacks(converter)

    events = []
    converter.set_runtime_callbacks(
        progress=lambda p: events.append(("progress", p)),
        error=lambda e: events.append(("error", e)),
        done=lambda: events.append(("done",)),
    )
    converter.add_data_file(["synth_0"])
    converter.wait_idle()

    assert converter.status() == dew.ConverterConversionStatus.completed
    assert ("done",) in events
    assert not [e for e in events if e[0] == "error"], events
    yield converter, cache
    del converter


def test_python_producer(synthetic_dataset):
    converter, _ = synthetic_dataset
    stats = converter.get_compression_stats()
    assert stats is not None
    assert stats.input_file_count == 1
    assert stats.attribute_count >= 1
    attribute_names = [a.name for a in stats.attributes]
    assert "xyz" in attribute_names
    # xyz is stored morton-packed (m32/m64), not as the i32x3 input layout
    xyz_stats = stats.attributes[attribute_names.index("xyz")]
    assert xyz_stats.buffer_count >= 1
    assert xyz_stats.uncompressed_bytes >= TOTAL * 4

    perf = converter.get_live_perf_stats()
    assert perf is not None
    assert perf.total_time_seconds >= 0.0


def test_written_values_reach_the_dataset(tmp_path):
    """A value-level check: identical points must compress far better than noise.

    If Python's writes never land in the converter's buffers, the library
    ingests uninitialized heap instead, which cannot compress like a constant
    cloud. This catches the whole 'buffer is a copy' failure class end to end,
    independently of the in-callback view assertions.
    """

    def make(path, filler):
        converter = dew.Converter(path, dew.ConverterOpenFileSemantics.truncate)

        def pre_init(filename):
            info = dew.ConverterFilePreInitInfo()
            info.approximate_point_count = TOTAL
            info.found_point_count = 1
            info.approximate_point_size_bytes = 16
            info.input_file_size_bytes = TOTAL * 16
            return info

        def init(filename, header, attributes):
            header.point_count = TOTAL
            header.offset = [0.0, 0.0, 0.0]
            header.scale = [0.001, 0.001, 0.001]
            header.min = [0.0, 0.0, 0.0]
            header.max = [1.0, 1.0, 1.0]
            attributes.add_attribute(dew.ATTRIBUTE_XYZ, dew.Type.i32, dew.Components.components_3)
            return {"produced": 0}

        def convert_data(state, header, attributes, buffers, max_points):
            xyz = buffers[0]
            count = min(TOTAL - state["produced"], max_points, xyz.shape[0])
            filler(xyz, count, state["produced"])
            state["produced"] += count
            return count, state["produced"] == TOTAL

        converter.set_file_converter_callbacks(pre_init=pre_init, init=init, convert_data=convert_data)
        converter.add_data_file(["synth_0"])
        converter.wait_idle()
        assert converter.status() == dew.ConverterConversionStatus.completed
        stats = converter.get_compression_stats()
        assert stats is not None
        return stats.attributes[0].uncompressed_bytes, stats.attributes[0].compressed_bytes

    # every point at the same coordinate -> one repeated morton code
    uncompressed, compressed = make(str(tmp_path / "constant.dew"), lambda a, n, off: a[:n, :].fill(0))
    assert compressed > 0
    ratio = uncompressed / compressed
    assert ratio > 20.0, (
        f"constant point cloud only compressed {ratio:.1f}x ({uncompressed} -> {compressed} bytes); "
        "the converter is not seeing the values Python wrote"
    )


def test_convert_data_rejects_oversized_count(tmp_path):
    """A count larger than the buffers hold must be refused, not trusted.

    The reader resizes buffers to the reported count without reallocating, so
    an oversized value would make the sorter read past the allocation.
    """
    converter = dew.Converter(str(tmp_path / "oversized.dew"), dew.ConverterOpenFileSemantics.truncate)

    def pre_init(filename):
        return None

    def init(filename, header, attributes):
        header.point_count = TOTAL
        header.scale = [0.001, 0.001, 0.001]
        header.max = [1.0, 1.0, 1.0]
        attributes.add_attribute(dew.ATTRIBUTE_XYZ, dew.Type.i32, dew.Components.components_3)
        return None

    def convert_data(state, header, attributes, buffers, max_points):
        return max_points + 1000, True  # more than was allocated

    converter.set_file_converter_callbacks(pre_init=pre_init, init=init, convert_data=convert_data)
    errors = []
    converter.set_runtime_callbacks(error=lambda e: errors.append(e))
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert errors, "an oversized points_read must be reported as an error"
    assert "more points" in errors[0].message


def test_init_without_attributes_is_rejected(tmp_path):
    """Registering no attributes would be UB in the reader (attributes[0])."""
    converter = dew.Converter(str(tmp_path / "noattrs.dew"), dew.ConverterOpenFileSemantics.truncate)
    converter.set_file_converter_callbacks(
        pre_init=lambda filename: None,
        init=lambda filename, header, attributes: None,  # registers nothing
        convert_data=lambda *a: (0, True),
    )
    errors = []
    converter.set_runtime_callbacks(error=lambda e: errors.append(e))
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert errors
    assert "no attributes" in errors[0].message


def test_init_with_wrong_first_attribute_is_rejected(tmp_path):
    converter = dew.Converter(str(tmp_path / "badfirst.dew"), dew.ConverterOpenFileSemantics.truncate)

    def init(filename, header, attributes):
        attributes.add_attribute(dew.ATTRIBUTE_INTENSITY, dew.Type.u16, dew.Components.components_1)
        return None

    converter.set_file_converter_callbacks(
        pre_init=lambda filename: None, init=init, convert_data=lambda *a: (0, True)
    )
    errors = []
    converter.set_runtime_callbacks(error=lambda e: errors.append(e))
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert errors
    assert "ATTRIBUTE_XYZ" in errors[0].message


def test_destroy_callback_is_invoked(tmp_path):
    """The optional destroy callable must actually run when a file finishes."""
    converter = dew.Converter(str(tmp_path / "destroy.dew"), dew.ConverterOpenFileSemantics.truncate)
    _install_synthetic_callbacks(converter)
    destroyed = []

    def init(filename, header, attributes):
        header.point_count = TOTAL
        header.offset = [0.0, 0.0, 0.0]
        header.scale = [0.001, 0.001, 0.001]
        header.min = [0.0, 0.0, 0.0]
        header.max = [1.0, 1.0, 1.0]
        attributes.add_attribute(dew.ATTRIBUTE_XYZ, dew.Type.i32, dew.Components.components_3)
        return {"produced": 0, "name": filename}

    def convert_data(state, header, attributes, buffers, max_points):
        xyz = buffers[0]
        count = min(TOTAL - state["produced"], max_points, xyz.shape[0])
        xyz[:count, :].fill(1)
        state["produced"] += count
        return count, state["produced"] == TOTAL

    converter.set_file_converter_callbacks(
        pre_init=lambda filename: None,
        init=init,
        convert_data=convert_data,
        destroy=lambda state: destroyed.append(state["name"]),
    )
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert destroyed == ["synth_0"]


def _minimal_callbacks(header_filler=None):
    def pre_init(filename):
        return None

    def init(filename, header, attributes):
        if header_filler is None:
            header.point_count = TOTAL
            header.scale = [0.001, 0.001, 0.001]
            header.min = [0.0, 0.0, 0.0]
            header.max = [1.0, 1.0, 1.0]
        else:
            header_filler(header)
        attributes.add_attribute(dew.ATTRIBUTE_XYZ, dew.Type.i32, dew.Components.components_3)
        return None

    def convert_data(state, header, attributes, buffers, max_points):
        return 0, True

    return pre_init, init, convert_data


def _run_expecting_error(tmp_path, name, header_filler):
    converter = dew.Converter(str(tmp_path / name), dew.ConverterOpenFileSemantics.truncate)
    pre_init, init, convert_data = _minimal_callbacks(header_filler)
    converter.set_file_converter_callbacks(pre_init=pre_init, init=init, convert_data=convert_data)
    errors = []
    converter.set_runtime_callbacks(error=lambda e: errors.append(e))
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    return errors


def test_init_must_set_scale(tmp_path):
    """A zeroed scale would multiply every coordinate to zero -- silent collapse."""

    def leave_scale_zero(header):
        header.point_count = TOTAL
        header.min = [0.0, 0.0, 0.0]
        header.max = [1.0, 1.0, 1.0]

    errors = _run_expecting_error(tmp_path, "noscale.dew", leave_scale_zero)
    assert errors
    assert "header.scale" in errors[0].message


def test_init_rejects_inverted_bounds(tmp_path):
    def inverted(header):
        header.point_count = TOTAL
        header.scale = [0.001, 0.001, 0.001]
        header.min = [10.0, 10.0, 10.0]
        header.max = [0.0, 0.0, 0.0]

    errors = _run_expecting_error(tmp_path, "inverted.dew", inverted)
    assert errors
    assert "greater than" in errors[0].message


def test_init_objects_survive_the_callback(tmp_path):
    """`header` and `attributes` must not alias the reader thread's stack.

    Retaining them is an ordinary Python reflex; if they were raw references to
    reader locals, reading them afterwards would return garbage and calling
    add_attribute would write through a dangling pointer (segfault).
    """
    converter = dew.Converter(str(tmp_path / "retain.dew"), dew.ConverterOpenFileSemantics.truncate)
    kept = {}

    def init(filename, header, attributes):
        header.point_count = TOTAL
        header.scale = [0.001, 0.001, 0.001]
        header.min = [0.0, 0.0, 0.0]
        header.max = [1.0, 1.0, 1.0]
        attributes.add_attribute(dew.ATTRIBUTE_XYZ, dew.Type.i32, dew.Components.components_3)
        kept["header"] = header
        kept["attributes"] = attributes
        return None

    converter.set_file_converter_callbacks(
        pre_init=lambda filename: None, init=init, convert_data=lambda *a: (0, True)
    )
    converter.add_data_file(["synth_0"])
    converter.wait_idle()

    # the retained header is an independent Python object, not stack memory
    assert kept["header"].point_count == TOTAL
    assert kept["header"].scale == pytest.approx([0.001, 0.001, 0.001])
    assert kept["attributes"].count == 1
    # ... and the registrar refuses to write to the finished file's attributes
    with pytest.raises(ValueError):
        kept["attributes"].add_attribute("late", dew.Type.u16, dew.Components.components_1)


def test_dropping_a_busy_converter_is_safe(tmp_path):
    """Python destroys implicitly, so the holder must quiesce the pipeline.

    Tearing the converter down mid-flight aborts the process inside the thread
    pool (an enqueue after the pool stopped), which no Python-level handler can
    catch -- the destructor drains first.
    """
    converter = dew.Converter(str(tmp_path / "busy.dew"), dew.ConverterOpenFileSemantics.truncate)
    _install_synthetic_callbacks(converter)
    converter.add_data_file(["synth_0"])
    del converter  # no wait_idle: the drain happens in the destructor


def test_python_producer_error_propagates(tmp_path):
    converter = dew.Converter(str(tmp_path / "err.dew"), dew.ConverterOpenFileSemantics.truncate)

    def bad_pre_init(filename):
        raise RuntimeError("boom from python")

    converter.set_file_converter_callbacks(
        pre_init=bad_pre_init,
        init=lambda *a: None,
        convert_data=lambda *a: (0, True),
    )
    errors = []
    converter.set_runtime_callbacks(error=lambda e: errors.append(e))
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert errors, "python exception should surface through the error callback"
    assert "boom from python" in errors[0].message


def test_python_producer_init_failure(tmp_path):
    converter = dew.Converter(str(tmp_path / "err2.dew"), dew.ConverterOpenFileSemantics.truncate)

    def pre_init(filename):
        return None  # zeroed info is acceptable

    def bad_init(filename, header, attributes):
        raise RuntimeError("init exploded")

    converter.set_file_converter_callbacks(
        pre_init=pre_init,
        init=bad_init,
        convert_data=lambda *a: (0, True),
    )
    errors = []
    converter.set_runtime_callbacks(error=lambda e: errors.append(e))
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert errors
    assert "init exploded" in errors[0].message


# ---------------------------------------------------------------------------
# read-back through the render-tier data source
# ---------------------------------------------------------------------------


def test_read_back(synthetic_dataset):
    _, cache = synthetic_dataset
    renderer = dew.Renderer()
    source = dew.ConverterDataSource(cache, renderer)

    assert source.attribute_count() >= 1
    names = [source.get_attribute_name(i) for i in range(source.attribute_count())]
    assert "xyz" in names

    fired = threading.Event()
    box = {}

    def on_aabb(aabb_min, aabb_max):
        box["min"], box["max"] = aabb_min, aabb_max
        fired.set()

    source.request_aabb(on_aabb)
    assert fired.wait(timeout=15.0), "request_aabb callback never fired"
    # Fires from a converter-internal thread with two 3-tuples. This is the
    # dataset's morton (Z-order) extent, NOT an exact bounding box: it decodes
    # the smallest and largest Z-order codes, which are real points near but
    # generally not at the corners. So assert it is a sane, non-degenerate box
    # contained in what the producer wrote (x = 0..TOTAL-1, y = i%7, z = i%3 at
    # scale 0.001) -- that still pins the value-level round trip through the
    # converter, the file, and the read path.
    assert len(box["min"]) == 3 and len(box["max"]) == 3
    lo, hi = list(box["min"]), list(box["max"])
    assert hi[0] > lo[0], "the x extent must be non-degenerate"
    for axis, limit in enumerate(((TOTAL - 1) * 0.001, 6 * 0.001, 2 * 0.001)):
        assert -1e-6 <= lo[axis] <= limit + 1e-6, f"min[{axis}] outside the written data"
        assert -1e-6 <= hi[axis] <= limit + 1e-6, f"max[{axis}] outside the written data"
    # x dominates the Z-order here, so its extreme is the true one
    assert hi[0] == pytest.approx((TOTAL - 1) * 0.001, abs=1e-3)

    # renderer.add_data_source flows the by-value token struct through
    renderer.add_data_source(source.get())
    renderer.add_data_source(source.get_bbox_data_source())


def test_read_back_missing_dataset():
    renderer = dew.Renderer()
    with pytest.raises(dew.Error):
        dew.ConverterDataSource("/nonexistent/nowhere.dew", renderer)
