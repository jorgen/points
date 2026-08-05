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

#include "pump.hpp"

struct dew_pump_t
{
  dew::core::waker_t waker;
  std::mutex sources_mutex;
  std::vector<dew::core::pump_source_t> sources;
};

namespace dew::core
{

void pump_register(dew_pump_t *pump, const pump_source_t &source)
{
  if (!pump || !source.ctx)
    return;
  std::unique_lock<std::mutex> lock(pump->sources_mutex);
  for (auto &existing : pump->sources)
  {
    if (existing.ctx == source.ctx)
    {
      existing = source;
      return;
    }
  }
  pump->sources.push_back(source);
}

void pump_unregister(dew_pump_t *pump, void *ctx)
{
  if (!pump)
    return;
  std::unique_lock<std::mutex> lock(pump->sources_mutex);
  for (auto it = pump->sources.begin(); it != pump->sources.end(); ++it)
  {
    if (it->ctx == ctx)
    {
      pump->sources.erase(it);
      return;
    }
  }
}

void pump_fire(dew_pump_t *pump)
{
  if (pump)
    pump->waker.fire();
}

} // namespace dew::core

using namespace dew::core;

struct dew_pump_t *dew_pump_create(void)
{
  return new dew_pump_t();
}

void dew_pump_destroy(struct dew_pump_t *pump)
{
  if (!pump)
    return;
  // Stop firing and wait out any wake already in flight before anything is torn down. Subsystems are
  // expected to have unregistered (and quiesced their own loops) by now -- the same ordered teardown
  // the processor uses.
  pump->waker.detach();
  delete pump;
}

void dew_pump_set_wake_callback(struct dew_pump_t *pump, dew_wake_callback_t callback, void *user_ptr)
{
  if (!pump)
    return;
  if (callback)
    pump->waker.set_callback(callback, user_ptr);
  else
    pump->waker.detach();
}

uint32_t dew_pump_poll(struct dew_pump_t *pump)
{
  if (!pump)
    return 0;

  // Rearm FIRST. See the contract in pump.h: rearming after the dispatch would swallow any
  // completion that lands while we are dispatching.
  pump->waker.rearm();

  // Drain a snapshot, not the live vector: a drain callback may close its subsystem and unregister
  // itself, which would invalidate the iterator underneath us.
  std::vector<pump_source_t> snapshot;
  {
    std::unique_lock<std::mutex> lock(pump->sources_mutex);
    snapshot = pump->sources;
  }

  uint32_t dispatched = 0;
  for (const auto &source : snapshot)
  {
    if (source.drain)
      dispatched += source.drain(source.ctx);
  }
  return dispatched;
}

uint32_t dew_pump_pending_count(struct dew_pump_t *pump)
{
  if (!pump)
    return 0;
  std::vector<pump_source_t> snapshot;
  {
    std::unique_lock<std::mutex> lock(pump->sources_mutex);
    snapshot = pump->sources;
  }
  uint32_t pending = 0;
  for (const auto &source : snapshot)
  {
    if (source.pending)
      pending += source.pending(source.ctx);
  }
  return pending;
}
