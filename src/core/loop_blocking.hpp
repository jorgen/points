/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
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

// Running a storage coroutine from a thread that is not the loop's, and waiting for it.
//
// This is the seam between the two ways into the storage backends. Everything real is a coroutine on
// an event loop; the synchronous entry points (storage_backend_t::exists / read_index, called once at
// open from a constructing thread) are thin wrappers around the same coroutines, waiting here. Having
// one implementation of the wait means the two backends cannot drift apart on the tricky parts --
// coroutine-frame lifetime, and the fact that wasm has to wait completely differently.

#include <dew/core/error.h>

#include <vio/event_loop.h>
#include <vio/task.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h> // emscripten_sleep (Asyncify)
#endif

#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

namespace dew::core
{

namespace loop_blocking_detail
{
struct sync_wait_state_t
{
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  dew_error_t result;
};

// The coroutine that actually drives the io. state and factory are BY-VALUE parameters so they are
// copied into the coroutine frame (a lambda's captures instead live in the closure temporary, which
// is destroyed after the first suspension -> use-after-free on resume). Matches the handle_* pattern.
template <typename Factory>
vio::task_t<void> sync_wait_coro(std::shared_ptr<sync_wait_state_t> state, Factory factory)
{
  auto err = co_await factory();
  {
    std::unique_lock<std::mutex> lk(state->m);
    state->result = std::move(err);
    state->done = true;
  }
  state->cv.notify_one();
  co_return;
}
} // namespace loop_blocking_detail

// Run a coroutine (returning dew_error_t) on `loop` and block the calling thread until it completes.
//
// ONLY for the one-time bootstrap calls -- exists() / read_index() -- invoked from a constructing or
// main thread. Calling this FROM `loop` deadlocks: it would wait on work it is itself blocking. Loop-side
// code awaits the _async form instead.
//
// The shared state outlives both sides, so the sync objects stay alive even if the caller returns
// first; that is what makes teardown race-free.
template <typename Factory>
dew_error_t run_on_loop_blocking(vio::event_loop_t &loop, Factory factory)
{
  auto state = std::make_shared<loop_blocking_detail::sync_wait_state_t>();
  // The lambda handed to run_in_loop is NOT a coroutine: it just forwards into sync_wait_coro, whose
  // by-value parameters own copies of state/factory for the lifetime of the actual io.
  loop.run_in_loop([state, factory = std::move(factory)]() mutable -> vio::task_t<void> { return loop_blocking_detail::sync_wait_coro(state, std::move(factory)); });
#ifdef __EMSCRIPTEN__
  // Single-threaded wasm: no other thread can ever satisfy a condition_variable, so a cv.wait here
  // would deadlock. Instead pump `loop` ourselves (there is no separate loop thread to make progress)
  // and yield to the browser between passes so the pending emscripten_fetch/XHR callbacks can run and
  // post their coroutine resumes back onto the loop. Requires -sASYNCIFY, which is exactly why the
  // query engine never comes through here (see src/wasm/access_noasyncify_probe.cpp).
  while (!state->done)
  {
    loop.poll();
    emscripten_sleep(0);
  }
#else
  std::unique_lock<std::mutex> lk(state->m);
  state->cv.wait(lk, [&] { return state->done; });
#endif
  return state->result;
}

} // namespace dew::core
