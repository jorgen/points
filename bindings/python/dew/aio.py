"""asyncio integration for dewfall.

``Dataset.query_box()`` blocks the calling thread. That is the right default for a script and the
wrong shape for an event loop: called from a coroutine it stalls everything -- no other task, no
timer, no socket.

The library is built for the other shape too. dewfall never calls user code on its own threads;
instead it *signals* (a pump wake callback) and the host *dispatches* (``Pump.poll``) on whatever
thread it likes::

    wake (library thread) -> loop.call_soon_threadsafe -> poll (loop thread) -> resolve futures

This module is that handshake, so callers do not each rewrite it::

    import dew.aio

    async with dew.aio.open_dataset("scan.dew") as ds:
        result = await ds.query_box([0, 0, 0], [10, 10, 10], attributes=["intensity"])

Several queries can be in flight at once (``asyncio.gather``), and the loop keeps serving everything
else while they run. Use a :class:`Session` directly when you want several datasets driven by ONE
pump, which is what the pump is for -- a single wake, and a single poll that drains everything.

If a thread per query is acceptable, ``await asyncio.to_thread(ds.query_box, ...)`` is a perfectly
good answer and needs none of this. This module is for when it is not.
"""

import asyncio

from . import _dew

__all__ = ["Session", "AsyncDataset", "open_dataset"]


class Session:
    """A :class:`dew.Pump` wired to the running asyncio loop.

    Datasets opened through one session share its pump, so one wake drains all of them. Create it
    from inside a running loop; it binds to that loop.
    """

    def __init__(self):
        self._loop = asyncio.get_running_loop()
        self.pump = _dew.Pump()
        # Things waiting on a state only visible after a poll: (predicate, future).
        self._waiters = []
        self._closed = False
        self.pump.set_wake_callback(self._on_wake)

    def close(self):
        """Detach from the pump. Idempotent.

        Detaching WAITS for any wake already in flight to return, so the library cannot call into a
        loop that is going away.
        """
        if self._closed:
            return
        self._closed = True
        self.pump.set_wake_callback(None)
        for _, future in self._waiters:
            if not future.done():
                future.cancel()
        self._waiters.clear()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
        return False

    # -- internals ---------------------------------------------------------------------------

    def _on_wake(self):
        """Runs on a LIBRARY thread. Signal only: no dewfall calls, no touching _waiters."""
        try:
            self._loop.call_soon_threadsafe(self._drain)
        except RuntimeError:
            pass  # loop already closed; nothing left to notify

    def _drain(self):
        """Runs on the loop thread: dispatch, then resolve whatever became true."""
        if self._closed:
            return
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
        # One drain up front closes the race where the completion landed -- and its single wake was
        # already consumed -- between the caller's last look and this await.
        self._drain()
        await future

    async def open(self, url, connection="", options=None):
        """Open a dataset on this session's pump. Returns an :class:`AsyncDataset`."""
        dataset = _dew.Dataset(url, connection, options or _dew.DatasetOptions(), self.pump)
        # Always `opening` here: create() returns before the index is read, which is the property
        # the whole design rests on -- opening a remote dataset costs a round trip, and paying it in
        # the constructor would block every caller, browsers included.
        await self._until(lambda: dataset.state() != _dew.DatasetState.opening)
        if dataset.state() != _dew.DatasetState.ready:
            # get_error() RAISES dew.Error (code + message) rather than returning one. That is the
            # library's own exception type and carries more than a wrapper would, so let it out --
            # the raise below is only a fallback for the impossible case where it does not.
            dataset.get_error()
            raise RuntimeError(f"could not open {url}")
        return AsyncDataset(self, dataset)


class AsyncDataset:
    """A dataset whose queries are awaited rather than waited on.

    Everything that does not touch storage (``get_info``, ``attribute_count``,
    ``get_attribute_name``) is a plain synchronous call and is forwarded unchanged.
    """

    def __init__(self, session, dataset):
        self._session = session
        self._dataset = dataset
        self._closed = False

    @property
    def raw(self):
        """The underlying :class:`dew.Dataset`, for anything this wrapper does not forward."""
        return self._dataset

    def get_info(self):
        return self._dataset.get_info()

    def attribute_count(self):
        return self._dataset.attribute_count()

    def get_attribute_name(self, index):
        return self._dataset.get_attribute_name(index)

    def close(self):
        if self._closed:
            return
        self._closed = True
        self._dataset.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_):
        self.close()
        return False

    async def query_box(self, aabb_min, aabb_max, **kwargs):
        """Await the points inside a box.

        Same arguments and same return value as :meth:`dew.Dataset.query_box` -- a dict of NumPy
        arrays the caller owns -- except that the event loop keeps running while the query does.
        """
        request = self._dataset.query_box_submit(aabb_min, aabb_max, **kwargs)
        try:
            await self._session._until(lambda: request.done)
            return request.result()
        except asyncio.CancelledError:
            # Tell the engine to stop rather than leaving it to finish work nobody wants. The
            # terminal status still has to be observed through a poll, which release() handles.
            request.cancel()
            raise
        finally:
            # Until it is released the dataset keeps the request and its decoded points alive, so a
            # long-lived dataset issuing many queries would hold every one of them.
            request.release()


class _OwnedSession:
    """Async context manager for the one-dataset case: opens a private session and closes both."""

    def __init__(self, url, connection, options):
        self._url = url
        self._connection = connection
        self._options = options
        self._session = None
        self._dataset = None

    async def __aenter__(self):
        self._session = Session()
        try:
            self._dataset = await self._session.open(self._url, self._connection, self._options)
        except BaseException:
            self._session.close()
            self._session = None
            raise
        return self._dataset

    async def __aexit__(self, *_):
        if self._dataset is not None:
            self._dataset.close()
        if self._session is not None:
            self._session.close()
        return False


def open_dataset(url, connection="", options=None):
    """Open one dataset with a private pump::

        async with dew.aio.open_dataset("scan.dew") as ds:
            result = await ds.query_box(lo, hi)

    Use a :class:`Session` instead when several datasets should share one pump.
    """
    return _OwnedSession(url, connection, options)
