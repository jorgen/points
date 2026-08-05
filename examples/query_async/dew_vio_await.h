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

// co_await for dew_access, on a vio event loop.
//
// dew_access is a C API built around a wake callback and a poll: the library signals that something is
// ready, and the host dispatches it on its own thread by calling dew_pump_poll. That is deliberately
// the smallest contract that works everywhere -- but it is not how you want to WRITE the calling code.
// This header is the adapter, and it is the whole point of the example: about a hundred lines turn
// "register a callback, poll a queue" into "co_await".
//
// The bridge is two steps, and the split matters:
//
//   1. The wake fires on an arbitrary internal thread and may only SIGNAL. So it does one thing: post
//      to the vio loop. Nothing else is legal there -- not polling, not touching the dataset.
//   2. On the loop, dew_pump_poll runs. Completion callbacks fire from inside it, on the loop thread,
//      and resume the suspended coroutines.
//
// Everything the coroutines touch therefore lives on one thread, which is what makes the awaiters
// below free of locks and free of races. See the note on dew_request_awaiter_t::await_suspend for the
// one place that reasoning is load-bearing.

#include <dew/access/query.h>
#include <dew/core/pump.h>

#include <vio/event_loop.h>
#include <vio/task.h>

#include <coroutine>
#include <vector>

namespace dew_await
{

// Owns a pump and wires it to a vio event loop. One of these drives any number of datasets: the pump
// is shared, which is exactly what it is for -- a single wake, and a single poll that drains
// everything registered with it.
class loop_t
{
public:
  explicit loop_t(vio::event_loop_t &loop)
    : _loop(loop)
    , _pump(dew_pump_create())
  {
    dew_pump_set_wake_callback(_pump, &loop_t::on_wake, this);
  }

  ~loop_t()
  {
    // Detach BEFORE destroying anything: dew_pump_set_wake_callback(NULL) waits out a wake that is
    // already in flight, so a late completion cannot post into a half-destroyed object.
    dew_pump_set_wake_callback(_pump, nullptr, nullptr);
    dew_pump_destroy(_pump);
  }

  loop_t(const loop_t &) = delete;
  loop_t &operator=(const loop_t &) = delete;

  // Pass this to dew_dataset_create. Sharing one pump across datasets is the supported arrangement;
  // note that dew_dataset_poll then returns 0 by contract -- draining one subsystem of a shared pump
  // is the partial drain that loses wakeups. Poll the pump, not the dataset.
  dew_pump_t *pump() { return _pump; }
  vio::event_loop_t &event_loop() { return _loop; }

  // Run one dispatch pass on the loop thread. Public because a host with its own frame loop (a
  // renderer, say) may want to drive it from there instead of waiting to be woken.
  void drain()
  {
    dew_pump_poll(_pump); // request `done` callbacks fire from in here, and resume their coroutines

    // Dataset open has no completion callback in the C API -- readiness is a state you observe. So the
    // waiters are re-checked after every poll instead. Resume from a snapshot: a resumed coroutine
    // runs to its next suspension immediately and may register a new waiter, which would invalidate
    // an iterator over the live vector.
    std::vector<std::coroutine_handle<>> ready;
    for (size_t i = 0; i < _state_waiters.size();)
    {
      if (dew_dataset_state(_state_waiters[i].dataset) != dew_dataset_opening)
      {
        ready.push_back(_state_waiters[i].continuation);
        _state_waiters[i] = _state_waiters.back();
        _state_waiters.pop_back();
      }
      else
      {
        ++i;
      }
    }
    for (auto continuation : ready)
      continuation.resume();
  }

  void add_state_waiter(dew_dataset_t *dataset, std::coroutine_handle<> continuation) { _state_waiters.push_back({dataset, continuation}); }

private:
  static void on_wake(void *user_ptr)
  {
    // ARBITRARY THREAD, and under wasm possibly re-entrant. Post and return -- that is the entire
    // legal vocabulary here. run_in_loop is thread-safe and is the whole reason this is sound.
    auto *self = static_cast<loop_t *>(user_ptr);
    self->_loop.run_in_loop([self]() { self->drain(); });
  }

  struct state_waiter_t
  {
    dew_dataset_t *dataset;
    std::coroutine_handle<> continuation;
  };

  vio::event_loop_t &_loop;
  dew_pump_t *_pump;
  std::vector<state_waiter_t> _state_waiters;
};

// co_await this to wait for a dataset to finish opening. Resumes with the terminal state, so the
// caller still has to distinguish ready from error.
class dataset_ready_awaiter_t
{
public:
  dataset_ready_awaiter_t(loop_t &driver, dew_dataset_t *dataset)
    : _driver(driver)
    , _dataset(dataset)
  {
  }

  bool await_ready() const noexcept { return dew_dataset_state(_dataset) != dew_dataset_opening; }
  void await_suspend(std::coroutine_handle<> continuation) { _driver.add_state_waiter(_dataset, continuation); }
  dew_dataset_state_t await_resume() const noexcept { return dew_dataset_state(_dataset); }

private:
  loop_t &_driver;
  dew_dataset_t *_dataset;
};

inline dataset_ready_awaiter_t dataset_ready(loop_t &driver, dew_dataset_t *dataset)
{
  return {driver, dataset};
}

// co_await a request. Construct it, hand its done()/done_user_ptr to the request descriptor, then
// await it; it resumes with the terminal status.
//
// The awaiter must outlive the request, which is why it is a named local in the calling coroutine
// rather than a temporary -- a temporary would die at the end of the full-expression that created the
// request, and the done callback would then write into a dead object.
class request_awaiter_t
{
public:
  bool await_ready() const noexcept { return _finished; }

  bool await_suspend(std::coroutine_handle<> continuation)
  {
    // No lock, and no re-check race, because of where the two sides run. `done` fires ONLY from inside
    // dew_pump_poll, poll runs ONLY on the loop thread, and this coroutine is on that same thread --
    // so between creating the request and suspending here, no poll can have interleaved. If you ever
    // drive the pump from a different thread than the coroutine, this needs a mutex like the one in
    // blob_reader's awaiter.
    if (_finished)
      return false; // decline to suspend; carry on synchronously
    _continuation = continuation;
    return true;
  }

  dew_request_status_t await_resume() const noexcept { return _status; }

  // Wire these two into dew_region_request_t.
  static dew_request_done_callback_t done() { return &request_awaiter_t::on_done; }
  void *user_ptr() { return this; }

private:
  static void on_done(dew_request_t *request, dew_request_status_t status, void *user_ptr)
  {
    (void)request;
    auto *self = static_cast<request_awaiter_t *>(user_ptr);
    self->_status = status;
    self->_finished = true;
    if (self->_continuation)
    {
      // Runs on the loop thread inside dew_pump_poll, so resuming straight into the coroutine is
      // correct -- it continues on exactly the thread it suspended on.
      auto continuation = self->_continuation;
      self->_continuation = {};
      continuation.resume();
    }
  }

  std::coroutine_handle<> _continuation;
  dew_request_status_t _status = dew_request_pending;
  bool _finished = false;
};

} // namespace dew_await
