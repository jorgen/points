#!/usr/bin/env python3
"""Querying a dewfall dataset from asyncio, without ever blocking the event loop.

    python examples/python/query_asyncio.py <dataset.dew> [--connection SPEC]

`dew.Dataset.query_box()` is the easy path and the right default -- one call, arrays back. But it
blocks the calling thread until the query finishes. Called from a coroutine, that stalls the whole
event loop: no other task runs, no timer fires, no socket is served.

The library is built for the other shape too. dewfall never calls user code on its own threads;
instead it *signals* (a wake callback) and the host *dispatches* (Pump.poll) on whatever thread it
likes. That is precisely the handshake asyncio wants:

    wake (library thread)  ->  loop.call_soon_threadsafe  ->  poll (loop thread)  ->  resolve futures

DewAsync below is the whole adapter, about forty lines, and it turns that into plain `await`. The
demo then runs two queries concurrently with asyncio.gather, uses the coarse one to find where the
points actually are, and follows up with a third -- all while a heartbeat ticks in parallel. The
heartbeat is the evidence: it could not print at all during a blocking query.

For comparison, the same program written the easy way would be:

    result = await asyncio.to_thread(dataset.query_box, lo, hi)

which is a perfectly good answer when a thread per query is acceptable. The point of this example is
what to do when it is not: one loop, no extra threads, many queries in flight.
"""

import argparse
import asyncio
import os
import sys

try:
    import dew
except ImportError:  # running from a build tree rather than an installed wheel
    _here = os.path.dirname(os.path.abspath(__file__))
    for _candidate in ("cmake-build-debug", "cmake-build-release", "build"):
        _path = os.path.join(_here, "..", "..", _candidate, "bindings", "python")
        if os.path.isdir(_path):
            sys.path.insert(0, os.path.abspath(_path))
            break
    import dew


class DewAsync:
    """Drives a dew.Pump from an asyncio loop, exposing awaits instead of polls."""

    def __init__(self):
        self._loop = asyncio.get_running_loop()
        self.pump = dew.Pump()
        # Anything waiting on a state that only becomes visible after a poll: (predicate, future).
        self._waiters = []
        self.pump.set_wake_callback(self._on_wake)

    def close(self):
        # Detach first. This waits for any wake already in flight to return, so the library cannot
        # call into a loop we are about to stop using.
        self.pump.set_wake_callback(None)

    def _on_wake(self):
        """Runs on a LIBRARY thread. Signal only -- no dewfall calls, no touching self._waiters."""
        self._loop.call_soon_threadsafe(self._drain)

    def _drain(self):
        """Runs on the loop thread: dispatch, then resolve whatever became true."""
        self.pump.poll()
        pending = []
        for predicate, future in self._waiters:
            if future.done():
                continue
            if predicate():
                future.set_result(None)
            else:
                pending.append((predicate, future))
        self._waiters = pending

    async def _until(self, predicate):
        if predicate():
            return
        future = self._loop.create_future()
        self._waiters.append((predicate, future))
        # One drain up front closes the race where the completion landed (and its single wake was
        # already consumed) between the caller's last look and this await.
        self._drain()
        await future

    async def open(self, url, connection=""):
        dataset = dew.Dataset(url, connection, dew.DatasetOptions(), self.pump)
        # Always `opening` here: create() returns before the index is read. That is the property the
        # whole design rests on -- opening a remote dataset costs a network round trip, and paying it
        # inside the constructor would block every caller, browsers included.
        await self._until(lambda: dataset.state() != dew.DatasetState.opening)
        if dataset.state() != dew.DatasetState.ready:
            raise RuntimeError(f"could not open {url}: {dataset.get_error()}")
        return dataset

    async def query(self, dataset, aabb_min, aabb_max, **kwargs):
        request = dataset.query_box_submit(aabb_min, aabb_max, **kwargs)
        try:
            await self._until(lambda: request.done)
            return request.result()
        finally:
            # Until it is released the dataset holds the request and its decoded points.
            request.release()


async def heartbeat(stop):
    """Proof that the loop stayed live. A blocking query would silence this."""
    ticks = 0
    while not stop.is_set():
        await asyncio.sleep(0.002)
        ticks += 1
    print(f"heartbeat: the loop ran {ticks} times while the queries were in flight")


async def main():
    parser = argparse.ArgumentParser(description="Query a dewfall dataset from asyncio.")
    parser.add_argument("dataset", help="dataset URL or path")
    parser.add_argument("--connection", default="", help="cloud credentials for s3:// datasets")
    args = parser.parse_args()

    driver = DewAsync()
    try:
        dataset = await driver.open(args.dataset, args.connection)
        info = dataset.get_info()
        lo = list(info.aabb_min)
        hi = list(info.aabb_max)
        print(f"opened: cell {[round(v, 3) for v in lo]} .. {[round(v, 3) for v in hi]}")

        stop = asyncio.Event()
        beat = asyncio.create_task(heartbeat(stop))

        # Two queries at once. gather submits both before awaiting either, so their reads overlap
        # instead of running back to back -- which is the entire reason for doing this asynchronously.
        preview, full = await asyncio.gather(
            driver.query(dataset, lo, hi, lod="budget", max_points=50_000),
            driver.query(dataset, lo, hi, attributes=["intensity"]),
        )

        # The dataset's aabb is the octree CELL: a power-of-two cube that contains the points but can
        # be far larger than them. The documented way to find the real extent is a coarse query and
        # the bounds of what comes back -- so the preview above pays for itself twice.
        xyz = preview["xyz"]
        data_lo = xyz.min(axis=0)
        data_hi = xyz.max(axis=0)
        middle_lo = (data_lo + (data_hi - data_lo) * 0.25).tolist()
        middle_hi = (data_lo + (data_hi - data_lo) * 0.75).tolist()
        middle = await driver.query(dataset, middle_lo, middle_hi, attributes=["intensity"])

        stop.set()
        await beat

        for label, result in (("preview", preview), ("full", full), ("middle", middle)):
            keys = ", ".join(f"{k}{tuple(v.shape)}" for k, v in result.items() if hasattr(v, "shape"))
            print(f"{label:8} {result['point_count']:>8} points  {result['node_count']} node(s)  [{keys}]")

        print(f"data extent: {[round(float(v), 3) for v in data_lo]} .. {[round(float(v), 3) for v in data_hi]}")

        dataset.close()
    finally:
        driver.close()


if __name__ == "__main__":
    asyncio.run(main())
