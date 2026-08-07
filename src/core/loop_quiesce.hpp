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

// "Stop generating pool work" -- the teardown barrier every loop that enqueues onto the shared
// thread pool needs before that pool is joined.
//
// WHY IT EXISTS. vio::thread_pool_t::enqueue calls a bare abort() if the pool has been stopped
// (thread_pool.h). No message, no libc++abi banner -- just SIGABRT, which is what makes this class of
// bug so unpleasant to read off a CI log. processor_t::~processor_t joins the pool at step (2) but
// keeps the main and input event loops running until step (4), because the pool's parked tasks
// complete on the storage loop and post to the tree loop. That gap is real: any loop callback in it
// that reaches thread_pool.enqueue aborts the process.
//
// WHY A FLAG ALONE IS NOT ENOUGH, and why this helper waits. Setting the flag from the destructor
// thread races the loop: a callback may have already passed the check and not yet reached enqueue.
// Flipping it ON the loop and waiting for that task to run means every callback queued before it has
// finished (loops run callbacks in order), and every callback queued after it observes the flag. Only
// then is joining the pool safe.
//
// WHY NOT run_on_loop_blocking (loop_blocking.hpp). That one waits on a condition_variable natively
// and, under wasm, spins on emscripten_sleep -- i.e. it needs ASYNCIFY. Teardown runs in the browser
// on the non-ASYNCIFY render path, where the loop is COOPERATIVE (thread_with_event_loop_t spawns no
// thread), so nothing would ever dequeue the task while this thread sat on a future. That is not
// hypothetical: it is exactly why pressing Disconnect in the web viewer used to hang the page.

#include <vio/event_loop.h>

#ifdef __EMSCRIPTEN__
#include <vio/platform/wasm/event_loop_impl.h> // vio::wasm::pump
#else
#include <future>
#endif

#include <chrono>
#include <memory>
#include <utility>

namespace dew::core
{

// Run `fn` on `loop`, returning once it has run -- or once `timeout` expires, whichever comes first.
// Returns true if `fn` ran on the loop. Safe to call from any thread EXCEPT `loop`'s own (that would
// wait on work it is itself blocking).
//
// WHY THE WAIT IS BOUNDED, and why that is not a cop-out. This is used from destructors. An
// unbounded wait there converts every "the loop did not service its queue" condition -- a stopped
// loop, a loop thread wedged in a callback, a run_in_loop posted through an async handle that did
// not wake -- into a PERMANENT hang inside dew_converter_destroy. That is strictly worse than the
// enqueue-after-stop abort() the barrier exists to prevent: the abort is loud and bounded, the hang
// is silent and forever. Two ~2.5h Windows arm64 wheel jobs were burned learning that.
//
// So on timeout the caller carries on. It should still apply `fn`'s effect directly (setting an
// atomic flag is the only use here), which degrades to the pre-barrier behaviour -- a narrow race
// rather than a guaranteed deadlock. Callers that care can branch on the return value.
//
// The promise is heap-owned via shared_ptr because on the timeout path THIS FRAME GOES AWAY while
// the queued task may still be pending: a stack promise would be a use-after-free the moment the
// loop got around to running it. `fn` is copied into the task for the same reason.
template <typename Fn>
bool run_on_loop_and_wait(vio::event_loop_t &loop, Fn fn, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
#ifdef __EMSCRIPTEN__
  // The loop is COOPERATIVE here: thread_with_event_loop_t spawns no thread, so run_in_loop only
  // queues and nothing will ever dequeue it while this thread waits. Drive it ourselves instead --
  // waiting would hang the whole program, which is precisely what tearing a data source down in the
  // browser used to do: press Disconnect, and the page stops responding.
  (void)timeout;
  auto finished = std::make_shared<bool>(false);
  loop.run_in_loop([fn, finished]() {
    fn();
    *finished = true;
  });
  for (int i = 0; i < 1024 && !*finished; ++i)
    vio::wasm::pump();
  return *finished;
#else
  auto done = std::make_shared<std::promise<void>>();
  auto fut = done->get_future();
  loop.run_in_loop([fn, done]() {
    fn();
    done->set_value();
  });
  return fut.wait_for(timeout) == std::future_status::ready;
#endif
}

} // namespace dew::core
