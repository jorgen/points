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

// The pump's contract.
//
// Two properties carry the whole design, and neither is visible by reading the call sites:
//
//   * ONE wake per burst. A hundred completions must not produce a hundred wakes.
//   * The rearm happens BEFORE the dispatch. Rearming afterwards loses wakeups -- a completion
//     landing mid-dispatch finds the signal still raised, is suppressed, and is then cleared by the
//     rearm, leaving queued work that nothing announces. That failure is invisible under light load
//     and shows up as a hang under real load, so it gets a dedicated test below.

#include <doctest/doctest.h>

#include "pump.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace dew::core;

namespace
{

// A subsystem stand-in: a queue of events that drain() dispatches.
struct fake_source_t
{
  dew_pump_t *pump = nullptr;
  std::atomic<uint32_t> queued{0};
  uint32_t drained = 0;
  // When set, the next drain() enqueues one more event and fires again -- standing in for a
  // completion that lands while the host is dispatching.
  bool inject_during_drain = false;

  void produce(uint32_t count = 1)
  {
    queued.fetch_add(count, std::memory_order_acq_rel);
    pump_fire(pump);
  }

  static uint32_t drain_fn(void *ctx)
  {
    auto *self = static_cast<fake_source_t *>(ctx);
    const uint32_t n = self->queued.exchange(0, std::memory_order_acq_rel);
    self->drained += n;
    if (self->inject_during_drain)
    {
      self->inject_during_drain = false;
      self->produce();
    }
    return n;
  }

  static uint32_t pending_fn(void *ctx)
  {
    return static_cast<fake_source_t *>(ctx)->queued.load(std::memory_order_acquire);
  }

  pump_source_t source() { return pump_source_t{this, &drain_fn, &pending_fn}; }
};

struct wake_counter_t
{
  std::atomic<int> count{0};
  static void on_wake(void *ctx) { static_cast<wake_counter_t *>(ctx)->count.fetch_add(1, std::memory_order_acq_rel); }
};

} // namespace

TEST_CASE("pump: a burst of completions produces exactly one wake")
{
  dew_pump_t *pump = dew_pump_create();
  wake_counter_t wakes;
  dew_pump_set_wake_callback(pump, &wake_counter_t::on_wake, &wakes);

  fake_source_t source;
  source.pump = pump;
  pump_register(pump, source.source());

  for (int i = 0; i < 100; i++)
    source.produce();

  REQUIRE(wakes.count.load() == 1);
  REQUIRE(dew_pump_pending_count(pump) == 100);

  REQUIRE(dew_pump_poll(pump) == 100);
  REQUIRE(source.drained == 100);
  REQUIRE(dew_pump_pending_count(pump) == 0);

  // Rearmed by the poll, so the next burst wakes again.
  source.produce();
  REQUIRE(wakes.count.load() == 2);

  pump_unregister(pump, &source);
  dew_pump_destroy(pump);
}

TEST_CASE("pump: a completion arriving mid-dispatch still gets a wake")
{
  // THE regression test for rearm-before-dispatch. With the rearm moved after the dispatch this
  // fails: the injected completion's fire() is suppressed by the still-raised signal, the rearm then
  // clears it, and the host is never told about work that is sitting in the queue.
  dew_pump_t *pump = dew_pump_create();
  wake_counter_t wakes;
  dew_pump_set_wake_callback(pump, &wake_counter_t::on_wake, &wakes);

  fake_source_t source;
  source.pump = pump;
  pump_register(pump, source.source());

  source.produce();
  REQUIRE(wakes.count.load() == 1);

  source.inject_during_drain = true;
  REQUIRE(dew_pump_poll(pump) == 1);

  // The injected event is queued...
  REQUIRE(dew_pump_pending_count(pump) == 1);
  // ...and a second wake was delivered for it, so the host knows to poll again.
  REQUIRE(wakes.count.load() == 2);

  REQUIRE(dew_pump_poll(pump) == 1);
  REQUIRE(dew_pump_pending_count(pump) == 0);

  pump_unregister(pump, &source);
  dew_pump_destroy(pump);
}

TEST_CASE("pump: one poll drains every registered source")
{
  // The reason the pump owns the poll rather than each subsystem: a single wake must correspond to a
  // single drain of EVERYTHING, or a source whose fire was coalesced away is never serviced.
  dew_pump_t *pump = dew_pump_create();
  wake_counter_t wakes;
  dew_pump_set_wake_callback(pump, &wake_counter_t::on_wake, &wakes);

  fake_source_t a, b;
  a.pump = b.pump = pump;
  pump_register(pump, a.source());
  pump_register(pump, b.source());

  a.produce(3);
  b.produce(5); // coalesced into a's wake

  REQUIRE(wakes.count.load() == 1);
  REQUIRE(dew_pump_poll(pump) == 8);
  REQUIRE(a.drained == 3);
  REQUIRE(b.drained == 5);

  pump_unregister(pump, &a);
  pump_unregister(pump, &b);
  dew_pump_destroy(pump);
}

TEST_CASE("pump: a source can unregister itself from inside its own drain")
{
  // A completion callback may close the subsystem it belongs to, which unregisters mid-iteration.
  // dew_pump_poll drains a snapshot precisely so that does not invalidate the walk underneath it.
  struct self_removing_t
  {
    dew_pump_t *pump = nullptr;
    int drains = 0;
    static uint32_t drain_fn(void *ctx)
    {
      auto *self = static_cast<self_removing_t *>(ctx);
      self->drains++;
      pump_unregister(self->pump, self);
      return 1;
    }
  };

  dew_pump_t *pump = dew_pump_create();
  self_removing_t source;
  source.pump = pump;
  pump_register(pump, pump_source_t{&source, &self_removing_t::drain_fn, nullptr});

  REQUIRE(dew_pump_poll(pump) == 1);
  REQUIRE(source.drains == 1);
  // Gone now, so a second poll does nothing.
  REQUIRE(dew_pump_poll(pump) == 0);
  REQUIRE(source.drains == 1);

  dew_pump_destroy(pump);
}

TEST_CASE("pump: works with no wake callback, and stops waking once detached")
{
  dew_pump_t *pump = dew_pump_create();
  fake_source_t source;
  source.pump = pump;
  pump_register(pump, source.source());

  // Polling is enough on its own; the wake is only an optimisation over spinning.
  source.produce(2);
  REQUIRE(dew_pump_poll(pump) == 2);

  wake_counter_t wakes;
  dew_pump_set_wake_callback(pump, &wake_counter_t::on_wake, &wakes);
  source.produce();
  REQUIRE(wakes.count.load() == 1);
  REQUIRE(dew_pump_poll(pump) == 1);

  // Passing null detaches: no further wakes, but the queue still drains.
  dew_pump_set_wake_callback(pump, nullptr, nullptr);
  source.produce();
  REQUIRE(wakes.count.load() == 1);
  REQUIRE(dew_pump_poll(pump) == 1);

  pump_unregister(pump, &source);
  dew_pump_destroy(pump);
}

TEST_CASE("pump: concurrent fires coalesce to one wake per poll cycle")
{
  dew_pump_t *pump = dew_pump_create();
  wake_counter_t wakes;
  dew_pump_set_wake_callback(pump, &wake_counter_t::on_wake, &wakes);

  fake_source_t source;
  source.pump = pump;
  pump_register(pump, source.source());

  // Completions really do arrive from several internal threads, so the arm-once guard has to be
  // atomic rather than merely single-threaded-correct.
  constexpr int threads = 8;
  constexpr int per_thread = 500;
  std::vector<std::thread> workers;
  for (int t = 0; t < threads; t++)
    workers.emplace_back([&source] {
      for (int i = 0; i < per_thread; i++)
        source.produce();
    });
  for (auto &worker : workers)
    worker.join();

  // Exactly one wake: nothing polled in between, so every fire after the first was coalesced.
  REQUIRE(wakes.count.load() == 1);
  REQUIRE(dew_pump_poll(pump) == threads * per_thread);
  REQUIRE(source.drained == threads * per_thread);

  pump_unregister(pump, &source);
  dew_pump_destroy(pump);
}

TEST_CASE("pump: detach waits for an in-flight wake before returning")
{
  // A wake already past the callback lookup must finish before teardown proceeds, or it runs against
  // a destroyed host. renderer_wasm.cpp guards the same failure by detaching its hook first.
  struct slow_wake_t
  {
    std::atomic<bool> entered{false};
    std::atomic<bool> finished{false};
    static void on_wake(void *ctx)
    {
      auto *self = static_cast<slow_wake_t *>(ctx);
      self->entered.store(true, std::memory_order_release);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      self->finished.store(true, std::memory_order_release);
    }
  };

  dew_pump_t *pump = dew_pump_create();
  slow_wake_t wake;
  dew_pump_set_wake_callback(pump, &slow_wake_t::on_wake, &wake);

  std::thread firing([pump] { pump_fire(pump); });
  while (!wake.entered.load(std::memory_order_acquire))
    std::this_thread::yield();

  // Detaching mid-callback must block until it returns.
  dew_pump_set_wake_callback(pump, nullptr, nullptr);
  REQUIRE(wake.finished.load(std::memory_order_acquire));

  firing.join();
  dew_pump_destroy(pump);
}
