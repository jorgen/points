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

#include <utility>

namespace dew::core
{

// Run `fn` on `loop` and return only once it has actually run. Safe to call from any thread EXCEPT
// `loop`'s own (that would wait on work it is itself blocking).
//
// Under wasm the caller drives the cooperative loop instead of waiting on it, bounded so teardown
// terminates even if the task somehow never lands.
template <typename Fn>
void run_on_loop_and_wait(vio::event_loop_t &loop, Fn &&fn)
{
#ifdef __EMSCRIPTEN__
  bool finished = false;
  loop.run_in_loop([&fn, &finished]() {
    fn();
    finished = true;
  });
  for (int i = 0; i < 1024 && !finished; ++i)
    vio::wasm::pump();
#else
  std::promise<void> done;
  auto fut = done.get_future();
  loop.run_in_loop([&fn, &done]() {
    fn();
    done.set_value();
  });
  fut.wait();
#endif
}

} // namespace dew::core
