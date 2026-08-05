/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#pragma once

// The dataset READ engine: an event-loop thread, a storage backend, the compressed and decompressed
// blob caches, and the one primitive every reader in the codebase ultimately calls --
// blob_reader_t::read(location).
//
// Extracted from storage_handler_t so that reading a dataset does not drag in the write pipeline
// (blob allocation, checkpointing, the cache tier, upload notification). storage_handler_t now
// COMPOSES a blob_reader_t and forwards to it, so the converter is unaffected; anything that only
// reads -- the query API, a decode worker, a CLI tool -- can own a blob_reader_t on its own.

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#include <vio/task.h>
#include <vio/thread_pool.h>

#include "dataset_types.hpp"
#include "error.hpp"
#include "lru_cache.hpp"
#include "perf_stats.hpp"
#include "storage_backend.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#ifdef __EMSCRIPTEN__
#include <coroutine>
#endif

namespace dew::core
{

struct cache_key_t
{
  uint32_t file_id;
  uint64_t offset;

  bool operator==(const cache_key_t &other) const
  {
    return file_id == other.file_id && offset == other.offset;
  }
};

struct cache_key_hash_t
{
  uint64_t operator()(const cache_key_t &k) const
  {
    uint64_t h = uint64_t(k.file_id) ^ (k.offset * 0x9e3779b97f4a7c15ULL);
    return h;
  }
};

struct cache_value_t
{
  std::shared_ptr<uint8_t[]> compressed_data;
  uint32_t compressed_size;
};

struct decompressed_cache_value_t
{
  std::shared_ptr<uint8_t[]> data;
  uint32_t size;
};

struct read_request_t
{
  void wait_for_read();
  void set_cancelled() { _cancelled.store(true, std::memory_order_relaxed); }
  bool is_cancelled() const { return _cancelled.load(std::memory_order_relaxed); }

  std::shared_ptr<uint8_t[]> buffer;
  dew_blob_t buffer_info;
  dew_error_t error;

  bool raw = false; // when set, read() returns the COMPRESSED bytes as-is (no decompress) -- used by the
                    // wasm decode-worker path, which decompresses off the main thread.
  bool _done = false;
  std::atomic_bool _cancelled{false};
  std::mutex _mutex;
  std::condition_variable _block_for_read;

  // Fired exactly once, from whichever thread completes the read (the storage loop on a miss, or the
  // CALLING thread when read() is satisfied straight from cache). It must therefore only enqueue work,
  // never run consumer code inline. Supplied through read_options_t::on_complete -- it cannot be
  // installed after read() returns, because three of the cache-hit paths complete before it does.
  std::function<void(read_request_t &)> _on_complete;

  // Rather than parking a thread in wait_for_read, a coroutine can co_await the read completing.
  // complete_read_request resumes this continuation on its owning loop, so nothing blocks. (The read
  // completes on the storage loop; the awaiting coroutine usually lives on another loop, hence the
  // explicit resume loop for a cross-loop hand-off.)
  std::coroutine_handle<> _continuation{};
  vio::event_loop_t *_continuation_loop = nullptr;

  // The hand-off has to be race-free, because a read can finish at ANY point relative to the
  // coroutine suspending -- including entirely inside read(), on the caller's own thread, when the
  // cache satisfies it. So await_ready and await_suspend both consult _done under the request's
  // mutex, and await_suspend returns bool so it can decline to suspend on a read that landed between
  // the two. Checking unlocked and always suspending would lose the resume outright: the
  // continuation would be stored after complete_read_request had already looked for it, and the
  // coroutine would hang forever. (Single-threaded wasm hides this, since completions only ever
  // arrive from the same cooperative loop.)
  struct awaiter_t
  {
    read_request_t *req;
    vio::event_loop_t *loop;

    bool await_ready() const noexcept
    {
      std::unique_lock<std::mutex> lock(req->_mutex);
      return req->_done;
    }
    bool await_suspend(std::coroutine_handle<> h) noexcept
    {
      std::unique_lock<std::mutex> lock(req->_mutex);
      if (req->_done)
        return false; // finished while we were getting here: carry on without suspending
      req->_continuation = h;
      req->_continuation_loop = loop;
      return true;
    }
    void await_resume() const noexcept {}
  };
  // Await a read that has ALREADY been issued. Prefer co_read(), which issues and awaits in one
  // step and therefore cannot be used incorrectly.
  awaiter_t await_on(vio::event_loop_t &loop) noexcept { return awaiter_t{this, &loop}; }
};

struct read_options_t
{
  // Return the COMPRESSED bytes as-is. The wasm decode worker uses this to inflate off the main thread.
  bool raw = false;
  // Decompress on the CALLING thread instead of hopping to the shared pool on a RAM-cache hit.
  //
  // Callers that already run ON a pool worker and then block in wait_for_read (the LOD and collapse
  // readers) MUST set this: with every pool thread parked in such a wait, the hopped decompress queues
  // behind the waiters and the pool deadlocks. It is also required whenever the pool has no workers at
  // all -- which is every Emscripten build, where vio::thread_pool_t runs tasks inline.
  bool decompress_inline = false;
  // See read_request_t::_on_complete.
  std::function<void(read_request_t &)> on_complete;
};

class blob_reader_t
{
public:
  blob_reader_t(const std::string &url, vio::thread_pool_t &thread_pool, perf_stats_t &perf_stats, vio::event_pipe_t<dew_error_t> &storage_error_pipe, dew_error_t &error);
  ~blob_reader_t();

  // Join the loop thread. Idempotent, and required before the caches / backend / any event pipe an
  // in-flight read touches is destroyed.
  void stop_loop();

  [[nodiscard]] bool file_exists() const { return _backend && _backend->exists(); }
  [[nodiscard]] std::string file_exists_error() const { return _backend ? _backend->exists_error() : std::string(); }
  [[nodiscard]] dew_error_t read_index(index_load_t &out);

  std::shared_ptr<read_request_t> read(storage_location_t location, read_options_t options = {});

  // The blob allocator reuses freed offsets, so a location previously read (and cached) may now hold
  // different bytes. The write side calls this before overwriting, so a later read cannot be served a
  // stale entry for the same (file_id, offset).
  void invalidate(storage_location_t location);

  void set_read_cache_size(uint64_t max_bytes);
  void set_decompressed_cache_size(uint64_t max_bytes);
  uint64_t read_cache_current_bytes();

  // The write side (storage_handler_t) shares this loop and backend rather than standing up its own.
  [[nodiscard]] storage_backend_t *backend() { return _backend.get(); }
  [[nodiscard]] const storage_backend_t *backend() const { return _backend.get(); }
  [[nodiscard]] vio::event_loop_t &event_loop() { return _event_loop; }
  [[nodiscard]] vio::thread_pool_t &thread_pool() { return _thread_pool; }
  [[nodiscard]] perf_stats_t &perf_stats() { return _perf_stats; }

private:
  void handle_read_request(std::shared_ptr<read_request_t> &&read_request, storage_location_t &&location);
  vio::task_t<void> do_read_request(std::shared_ptr<read_request_t> read_request, storage_location_t location);

  vio::thread_pool_t &_thread_pool;
  vio::thread_with_event_loop_t _event_loop_thread;
  vio::event_loop_t &_event_loop;
  std::unique_ptr<storage_backend_t> _backend;
  std::atomic<int> _reads_in_flight{0}; // do_read_request coroutines currently holding the backend/a connection
  perf_stats_t &_perf_stats;
  vio::event_pipe_t<dew_error_t> &_storage_error;
  vio::event_pipe_t<std::shared_ptr<read_request_t>, storage_location_t> _read_request_pipe;

  lru_cache_t<cache_key_t, cache_value_t, cache_key_hash_t> _read_cache;
  // Decompressed-side cache for the pool readers (LOD sampling, leaf splits, collapse merges):
  // they re-read the same big ingest chunks many times, and re-inflating a 64MB chunk per read
  // dominates conversion time. Populated only on the decompress_inline path.
  lru_cache_t<cache_key_t, decompressed_cache_value_t, cache_key_hash_t> _decompressed_cache;
};

// Issue a read and hand back an awaiter for it, resuming the coroutine on `resume_loop`. The request
// is written to `out` so the caller can read its buffer/error after the co_await.
//
//     std::shared_ptr<read_request_t> request;
//     co_await co_read(reader, location, {}, my_loop, request);
//     if (request->error.code == 0) use(request->buffer_info);
read_request_t::awaiter_t co_read(blob_reader_t &reader, storage_location_t location, read_options_t options, vio::event_loop_t &resume_loop, std::shared_ptr<read_request_t> &out);

// Mark a read complete and release everyone waiting on it: the condition variable (a thread parked in
// wait_for_read), the coroutine continuation (cooperative wasm builds), and the completion hook.
// Every path that finishes a read goes through here so no waiter can be missed.
void complete_read_request(read_request_t &r);

} // namespace dew::core
