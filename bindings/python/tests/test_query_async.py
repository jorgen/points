"""The non-blocking query path: Pump.set_wake_callback / Pump.poll / Dataset.query_box_submit.

query_box() blocks the calling thread, which is fine for a script and fatal for an event loop. The
async surface exists so a host can submit and then dispatch on its own thread. These tests pin what
that layer is actually responsible for:

* submit does NOT block -- the request comes back `pending`, and stays pending until somebody polls.
  If submitting quietly ran the query to completion, every await built on this would still "work"
  while having serialized the caller, and nothing would look wrong;

* the wake fires from a library thread, exactly once per burst, and is what tells a host to poll;

* the answer is identical to the blocking path. Two ways in, one engine -- so a caller can move
  between them without wondering whether the numbers change.

examples/python/query_asyncio.py is the same machinery wrapped into `await`.
"""

import threading
import time

import numpy as np
import pytest

import dew

from test_smoke import _install_synthetic_callbacks  # noqa: F401  (shared converter callbacks)

TOTAL = 4096


@pytest.fixture()
def dataset_path(tmp_path):
    path = str(tmp_path / "query_async.dew")
    converter = dew.Converter(path, dew.ConverterOpenFileSemantics.truncate)
    _install_synthetic_callbacks(converter)
    converter.set_node_point_limit(256)
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert converter.status() == dew.ConverterConversionStatus.completed
    del converter
    return path


def _poll_until(pump, predicate, timeout_s=60.0):
    """Drive the pump until `predicate` holds, or give up.

    A DEADLINE, not a fixed iteration count. The open and the query run on the dataset's own thread,
    so a spin of N polls is a race against that thread rather than a wait -- 20000 tight iterations
    take milliseconds and lose it on any machine whose IO is slower than the one it was written on.
    (Windows CI, as it turned out.) The sleep also drops the GIL, which is what lets a wake callback
    run at all.
    """
    deadline = time.monotonic() + timeout_s
    while not predicate() and time.monotonic() < deadline:
        pump.poll()
        time.sleep(0.001)
    return predicate()


def _open(pump, path):
    """Open through the pump, driving it by hand -- no blocking wait anywhere."""
    dataset = dew.Dataset(path, "", dew.DatasetOptions(), pump)
    assert _poll_until(pump, lambda: dataset.state() != dew.DatasetState.opening)
    assert dataset.state() == dew.DatasetState.ready
    return dataset


def _whole_box(dataset):
    info = dataset.get_info()
    pad = 1.0 + max(hi - lo for lo, hi in zip(info.aabb_min, info.aabb_max))
    return [v - pad for v in info.aabb_min], [v + pad for v in info.aabb_max]


def test_create_returns_before_the_dataset_is_open(dataset_path):
    # The property the whole async design rests on: constructing never does the index read. It is
    # cheap to state and easy to regress, since a synchronous open would leave every test passing.
    pump = dew.Pump()
    dataset = dew.Dataset(dataset_path, "", dew.DatasetOptions(), pump)
    assert dataset.state() == dew.DatasetState.opening
    assert _poll_until(pump, lambda: dataset.state() != dew.DatasetState.opening)
    assert dataset.state() == dew.DatasetState.ready
    dataset.close()


def test_submit_does_not_block_and_needs_a_poll(dataset_path):
    pump = dew.Pump()
    dataset = _open(pump, dataset_path)
    lo, hi = _whole_box(dataset)

    request = dataset.query_box_submit(lo, hi, lod="full", clip_points=False)
    # Submit returned while the query is still running. Were this ever to come back terminal, the
    # await in the asyncio example would be a no-op wrapped around a blocking call.
    assert request.status == dew.RequestStatus.pending
    assert not request.done

    assert _poll_until(pump, lambda: request.done)

    assert request.status == dew.RequestStatus.completed
    result = request.result()
    assert result["point_count"] == TOTAL
    request.release()
    dataset.close()


def test_result_before_completion_raises(dataset_path):
    pump = dew.Pump()
    dataset = _open(pump, dataset_path)
    lo, hi = _whole_box(dataset)

    request = dataset.query_box_submit(lo, hi, lod="full", clip_points=False)
    with pytest.raises(RuntimeError, match="pending"):
        request.result()

    assert _poll_until(pump, lambda: request.done)
    request.release()
    # Released is distinguishable from pending, and neither silently returns empty arrays.
    with pytest.raises(RuntimeError, match="released"):
        request.result()
    dataset.close()


def test_wake_fires_from_a_library_thread(dataset_path):
    # The wake is the only reason a host knows to poll. If it never fired, an event-loop integration
    # would sit idle forever while the data was ready -- a hang, not a wrong answer, so it needs its
    # own test rather than being implied by the polling loops above.
    woke = threading.Event()
    wake_threads = []

    def on_wake():
        wake_threads.append(threading.current_thread().ident)
        woke.set()

    pump = dew.Pump()
    pump.set_wake_callback(on_wake)
    dataset = dew.Dataset(dataset_path, "", dew.DatasetOptions(), pump)

    assert _poll_until(pump, lambda: dataset.state() != dew.DatasetState.opening)
    assert dataset.state() == dew.DatasetState.ready

    # WAIT rather than check. The state is published just BEFORE the wake is raised, so the loop above
    # can see `ready` and break while the library thread has not reached pump_fire yet -- and even once
    # it has, the callback has to take the GIL, which a tight polling loop is holding. Event.wait drops
    # the GIL, which is what lets the wake through.
    assert woke.wait(10.0), "opening completed but no wake was delivered"
    # It really did arrive from inside the library, not from our own poll call.
    assert any(ident != threading.current_thread().ident for ident in wake_threads)

    pump.set_wake_callback(None)  # detaches, waiting out anything in flight
    dataset.close()


def test_async_and_blocking_paths_agree(dataset_path):
    pump = dew.Pump()
    dataset = _open(pump, dataset_path)
    lo, hi = _whole_box(dataset)

    request = dataset.query_box_submit(lo, hi, lod="full", attributes=["intensity"], clip_points=False)
    assert _poll_until(pump, lambda: request.done)
    from_async = request.result()
    request.release()
    dataset.close()

    blocking = dew.open_dataset(dataset_path)
    from_blocking = blocking.query_box(lo, hi, lod="full", attributes=["intensity"], clip_points=False)

    assert from_async["point_count"] == from_blocking["point_count"] == TOTAL
    assert from_async["node_count"] == from_blocking["node_count"]
    # Byte-identical, not merely the same shape: the two entry points must reach the same engine.
    np.testing.assert_array_equal(from_async["xyz"], from_blocking["xyz"])
    np.testing.assert_array_equal(from_async["intensity"], from_blocking["intensity"])


def test_arrays_outlive_the_request(dataset_path):
    # Same ownership rule as query_box: the C result borrows request-owned memory, so these have to
    # be copies. Releasing first and reading after is what catches a view that dangles.
    pump = dew.Pump()
    dataset = _open(pump, dataset_path)
    lo, hi = _whole_box(dataset)

    request = dataset.query_box_submit(lo, hi, lod="full", clip_points=False)
    assert _poll_until(pump, lambda: request.done)
    xyz = request.result()["xyz"]
    expected = np.array(xyz, copy=True)
    request.release()
    dataset.close()

    # owndata is False because a nanobind capsule owns the buffer rather than numpy itself -- still
    # Python-managed, still freed by the interpreter. The property that matters is that the VALUES
    # survive the release, which a borrowed view into request memory would not.
    np.testing.assert_array_equal(xyz, expected)


def test_request_is_a_context_manager(dataset_path):
    pump = dew.Pump()
    dataset = _open(pump, dataset_path)
    lo, hi = _whole_box(dataset)

    with dataset.query_box_submit(lo, hi, lod="full", clip_points=False) as request:
        assert _poll_until(pump, lambda: request.done)
        assert request.result()["point_count"] == TOTAL

    # __exit__ released it, so the handle is spent.
    with pytest.raises(RuntimeError, match="released"):
        request.result()
    dataset.close()
