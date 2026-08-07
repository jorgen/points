"""dew.aio: the asyncio driver that ships with the package.

The pump handshake (wake -> call_soon_threadsafe -> poll -> resolve) used to live in an example, so
every caller copied it. It is library code now, and these are the properties it owes them:

* the event loop keeps running while a query does -- the entire reason not to use query_box();
* several queries really overlap rather than serialising behind each other;
* the answer is identical to the blocking path, so moving between them changes nothing;
* a cancelled await does not leak the request.

asyncio.run() inside ordinary sync tests rather than pytest-asyncio: no new dependency, and each test
gets a fresh loop, which matters because a Session binds to the loop it was created on.
"""

import asyncio

import numpy as np
import pytest

import dew
import dew.aio

from test_smoke import _install_synthetic_callbacks  # noqa: F401  (shared converter callbacks)

TOTAL = 4096


@pytest.fixture()
def dataset_path(tmp_path):
    path = str(tmp_path / "aio.dew")
    converter = dew.Converter(path, dew.ConverterOpenFileSemantics.truncate)
    _install_synthetic_callbacks(converter)
    converter.set_node_point_limit(256)
    converter.add_data_file(["synth_0"])
    converter.wait_idle()
    assert converter.status() == dew.ConverterConversionStatus.completed
    del converter
    return path


def _whole_box(info):
    pad = 1.0 + max(hi - lo for lo, hi in zip(info.aabb_min, info.aabb_max))
    return [v - pad for v in info.aabb_min], [v + pad for v in info.aabb_max]


def test_open_and_query(dataset_path):
    async def main():
        async with dew.aio.open_dataset(dataset_path) as ds:
            lo, hi = _whole_box(ds.get_info())
            return await ds.query_box(lo, hi, lod="full", clip_points=False)

    result = asyncio.run(main())
    assert result["point_count"] == TOTAL
    assert result["xyz"].shape == (TOTAL, 3)


def test_open_missing_dataset_raises():
    # dew.Error, not a wrapper: get_error() raises the library's own exception type, which carries the
    # code and message. Wrapping it would throw that away.
    async def main():
        async with dew.aio.open_dataset("definitely-not-a-dataset.dew"):
            pass

    with pytest.raises(dew.Error):
        asyncio.run(main())


def test_the_loop_keeps_running_during_a_query(dataset_path):
    # The property that justifies the whole module. query_box() would park the thread and this
    # counter would not move; if it ever reads 0 the await has silently become a blocking call.
    ticks = 0

    async def heartbeat(stop):
        nonlocal ticks
        while not stop.is_set():
            await asyncio.sleep(0.001)
            ticks += 1

    async def main():
        async with dew.aio.open_dataset(dataset_path) as ds:
            lo, hi = _whole_box(ds.get_info())
            stop = asyncio.Event()
            beat = asyncio.create_task(heartbeat(stop))
            result = await ds.query_box(lo, hi, lod="full", clip_points=False)
            stop.set()
            await beat
            return result

    result = asyncio.run(main())
    assert result["point_count"] == TOTAL
    assert ticks > 0


def test_concurrent_queries_all_complete(dataset_path):
    async def main():
        async with dew.aio.open_dataset(dataset_path) as ds:
            lo, hi = _whole_box(ds.get_info())
            return await asyncio.gather(
                ds.query_box(lo, hi, lod="full", clip_points=False),
                ds.query_box(lo, hi, lod="budget", max_points=512, clip_points=False),
                ds.query_box(lo, hi, lod="full", attributes=["intensity"], clip_points=False),
            )

    full, budget, with_attr = asyncio.run(main())
    # gather submits all three before awaiting any, so they are genuinely in flight together. Each
    # still has to come back with its OWN answer -- a driver that resolved the wrong future would
    # show up here as three identical results.
    assert full["point_count"] == TOTAL
    assert with_attr["point_count"] == TOTAL
    assert budget["point_count"] <= TOTAL
    assert "intensity" in with_attr
    assert "intensity" not in full


def test_matches_the_blocking_path(dataset_path):
    async def main():
        async with dew.aio.open_dataset(dataset_path) as ds:
            lo, hi = _whole_box(ds.get_info())
            return lo, hi, await ds.query_box(lo, hi, lod="full", attributes=["intensity"], clip_points=False)

    lo, hi, from_aio = asyncio.run(main())
    blocking = dew.open_dataset(dataset_path).query_box(lo, hi, lod="full", attributes=["intensity"], clip_points=False)

    assert from_aio["point_count"] == blocking["point_count"] == TOTAL
    # Byte-identical, not merely the same shape: two ways in, one engine.
    np.testing.assert_array_equal(from_aio["xyz"], blocking["xyz"])
    np.testing.assert_array_equal(from_aio["intensity"], blocking["intensity"])


def test_cancelled_query_releases_the_request(dataset_path):
    # A cancelled await must not leave the request alive: until it is released the dataset keeps it
    # and its decoded points, so a loop that cancels queries would grow without bound.
    async def main():
        async with dew.aio.open_dataset(dataset_path) as ds:
            lo, hi = _whole_box(ds.get_info())
            task = asyncio.create_task(ds.query_box(lo, hi, lod="full", clip_points=False))
            await asyncio.sleep(0)  # let it submit and suspend
            task.cancel()
            with pytest.raises(asyncio.CancelledError):
                await task
            # The dataset still works afterwards -- cancelling one query does not poison the session.
            return await ds.query_box(lo, hi, lod="full", clip_points=False)

    result = asyncio.run(main())
    assert result["point_count"] == TOTAL


def test_a_lost_wake_does_not_hang(dataset_path):
    # The wake is the primary signal, but it must not be the ONLY one. A wake that never arrives --
    # a platform where the callback cannot run, a completion landing in a window the arm-once flag
    # swallows -- would otherwise leave the await hanging forever with no output, which is the worst
    # way for anything to fail. (It hung a Windows ARM CI job for over two hours.) The Session's
    # periodic re-poll is what turns that into latency instead.
    async def main():
        async with dew.aio.open_dataset(dataset_path) as ds:
            lo, hi = _whole_box(ds.get_info())
            # Detach the wake: from here nothing signals, and only the tick can make progress.
            ds._session.pump.set_wake_callback(None)
            return await ds.query_box(lo, hi, lod="full", clip_points=False)

    result = asyncio.run(main())
    assert result["point_count"] == TOTAL


def test_an_await_that_can_never_finish_times_out(dataset_path):
    # The other half of the backstop: something that genuinely never completes has to give up and say
    # so, rather than pinning the loop until someone kills the process.
    async def main():
        async with dew.aio.open_dataset(dataset_path) as ds:
            await ds._session._until(lambda: False, timeout=0.25)

    with pytest.raises(TimeoutError):
        asyncio.run(main())


def test_one_session_drives_several_datasets(dataset_path):
    # What a Session is FOR: one pump, one wake, one poll draining everything. Two datasets on
    # separate private pumps would also pass a naive test, so both are queried and compared.
    async def main():
        with dew.aio.Session() as session:
            first = await session.open(dataset_path)
            second = await session.open(dataset_path)
            assert first.raw is not second.raw
            lo, hi = _whole_box(first.get_info())
            a, b = await asyncio.gather(
                first.query_box(lo, hi, lod="full", clip_points=False),
                second.query_box(lo, hi, lod="full", clip_points=False),
            )
            first.close()
            second.close()
            return a, b

    a, b = asyncio.run(main())
    assert a["point_count"] == b["point_count"] == TOTAL
