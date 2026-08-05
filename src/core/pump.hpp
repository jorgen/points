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

// Internal side of dew/core/pump.h: the arm-once waker, and the registration a subsystem uses to
// join a pump's drain.

#include <dew/core/pump.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace dew::core
{

// Arm-once wake signal.
//
// fire() invokes the callback at most once until rearm(), so a burst of completions coalesces into a
// single wake. Everything here is callable from any thread except detach(), which must not be called
// from inside the callback (it waits for in-flight invocations to finish and would deadlock on
// itself -- and under wasm the callback runs on the same thread, so that is a real possibility).
class waker_t
{
public:
  void set_callback(dew_wake_callback_t callback, void *user_ptr)
  {
    {
      std::unique_lock<std::mutex> lock(_mutex);
      _callback = callback;
      _user_ptr = user_ptr;
      _detached = false;
    }
    // A fresh callback starts able to fire, even if the previous one had raised the signal.
    rearm();
  }

  void fire()
  {
    if (_armed.exchange(true, std::memory_order_acq_rel))
      return; // already raised and not yet consumed: coalesce

    // Count the invocation so detach() can wait it out, then take the callback under the lock but
    // CALL it outside: holding the lock across user code would deadlock a callback that (against the
    // contract) called back in, and would serialize wakes against set_callback for no reason.
    _in_flight.fetch_add(1, std::memory_order_acq_rel);
    dew_wake_callback_t callback = nullptr;
    void *user_ptr = nullptr;
    {
      std::unique_lock<std::mutex> lock(_mutex);
      if (!_detached)
      {
        callback = _callback;
        user_ptr = _user_ptr;
      }
    }
    if (callback)
      callback(user_ptr);
    _in_flight.fetch_sub(1, std::memory_order_acq_rel);
  }

  void rearm() { _armed.store(false, std::memory_order_release); }

  // Stop firing, and do not return until any in-flight callback has finished. Without the wait, a
  // wake already past the lock could run against a half-destroyed host -- the failure
  // examples/renderer/renderer_wasm.cpp guards against by detaching its hook first.
  void detach()
  {
    {
      std::unique_lock<std::mutex> lock(_mutex);
      _detached = true;
      _callback = nullptr;
      _user_ptr = nullptr;
    }
    while (_in_flight.load(std::memory_order_acquire) > 0)
      std::this_thread::yield();
  }

  [[nodiscard]] bool armed() const { return _armed.load(std::memory_order_acquire); }

private:
  std::mutex _mutex;
  dew_wake_callback_t _callback = nullptr;
  void *_user_ptr = nullptr;
  bool _detached = false;
  std::atomic<bool> _armed{false};
  std::atomic<int> _in_flight{0};
};

// A subsystem's hook into a pump. `drain` dispatches whatever is ready and returns how many events
// it delivered; `pending` reports how many are waiting. Both run on the host thread from
// dew_pump_poll. Plain function pointers rather than std::function so the registration is trivially
// copyable and safe to snapshot while draining.
struct pump_source_t
{
  void *ctx = nullptr;
  uint32_t (*drain)(void *ctx) = nullptr;
  uint32_t (*pending)(void *ctx) = nullptr;
};

void pump_register(dew_pump_t *pump, const pump_source_t &source);
void pump_unregister(dew_pump_t *pump, void *ctx);
// Raise the pump's wake. Safe from any thread; coalesced until the next poll.
void pump_fire(dew_pump_t *pump);

} // namespace dew::core
