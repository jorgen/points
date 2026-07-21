/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#include "object_backend.hpp"

#include <condition_variable>

#include <fmt/core.h>

namespace points::converter
{

namespace
{
struct sync_wait_state_t
{
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  points_error_t result;
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

// Run a coroutine (returning points_error_t) on `loop` and block the calling thread until it
// completes. Used only for the one-time bootstrap calls (exists/read_index) which are invoked from
// the processor's constructor thread, not the loop thread. The shared state keeps the sync objects
// alive until both the caller and the loop coroutine are done, so teardown is race-free.
template <typename Factory>
points_error_t run_on_loop_blocking(vio::event_loop_t &loop, Factory factory)
{
  auto state = std::make_shared<sync_wait_state_t>();
  // The lambda handed to run_in_loop is NOT a coroutine: it just forwards into sync_wait_coro, whose
  // by-value parameters own copies of state/factory for the lifetime of the actual io.
  loop.run_in_loop([state, factory = std::move(factory)]() mutable -> vio::task_t<void> { return sync_wait_coro(state, std::move(factory)); });
  std::unique_lock<std::mutex> lk(state->m);
  state->cv.wait(lk, [&] { return state->done; });
  return state->result;
}
} // namespace

std::string object_backend_t::object_name(uint32_t id)
{
  return fmt::format("blob_{:08x}", id);
}

object_backend_t::object_backend_t(std::unique_ptr<io_manager_t> io, vio::event_loop_t &event_loop)
  : _io(std::move(io))
  , _event_loop(event_loop)
{
  object_info_t info;
  auto err = run_on_loop_blocking(_event_loop, [this, &info]() { return _io->object_info(k_manifest_name, info); });
  _exists = (err.code == 0) && info.exists;
}

object_backend_t::~object_backend_t()
{
}

bool object_backend_t::exists() const
{
  return _exists;
}

points_error_t object_backend_t::open_for_write(bool truncate)
{
  if (truncate)
  {
    std::unique_lock<std::mutex> lock(_mutex);
    _next_id = 0;
    _attributes_location = {};
    _stats_location = {};
    _perf_stats_location = {};
    _tree_registry_location = {};
    // Existing objects are not enumerated/removed here (no list op yet). The new manifest, written
    // last, references only new objects; old objects become orphans reclaimed by a future gc().
  }
  // The directory (file backend) is created lazily on the first write_object; nothing to open here.
  return {};
}

vio::task_t<points_error_t> object_backend_t::read_location(storage_location_t loc, std::unique_ptr<uint8_t[]> &buf, uint32_t &size)
{
  if (loc.size == 0)
  {
    buf.reset();
    size = 0;
    co_return points_error_t{};
  }
  buf = std::make_unique<uint8_t[]>(loc.size);
  uint32_t read_bytes = 0;
  auto err = co_await _io->read_object(object_name(loc.file_id), buf.get(), io_range_t{0, int64_t(loc.size)}, read_bytes);
  size = loc.size;
  co_return err;
}

vio::task_t<points_error_t> object_backend_t::do_read_index(index_load_t &out)
{
  auto manifest = std::make_shared<uint8_t[]>(k_serialized_index_size);
  uint32_t br = 0;
  auto err = co_await _io->read_object(k_manifest_name, manifest.get(), io_range_t{0, int64_t(k_serialized_index_size)}, br);
  if (err.code != 0)
    co_return err;

  storage_location_t free_blobs;
  storage_location_t attribute_configs;
  storage_location_t tree_registry;
  storage_location_t compression_stats;
  storage_location_t perf_stats;
  err = deserialize_index(manifest.get(), k_serialized_index_size, free_blobs, attribute_configs, tree_registry, compression_stats, perf_stats);
  if (err.code != 0)
    co_return err;

  _next_id = uint32_t(free_blobs.offset);
  _attributes_location = attribute_configs;
  _stats_location = compression_stats;
  _perf_stats_location = perf_stats;
  _tree_registry_location = tree_registry;

  // free_blobs carries only next_id in object mode; there is no free-blobs object.
  out.free_blobs.reset();
  out.free_blobs_size = 0;

  err = co_await read_location(attribute_configs, out.attribute_configs, out.attribute_configs_size);
  if (err.code != 0)
    co_return err;
  err = co_await read_location(tree_registry, out.tree_registry, out.tree_registry_size);
  if (err.code != 0)
    co_return err;
  err = co_await read_location(compression_stats, out.stats, out.stats_size);
  if (err.code != 0)
    co_return err;
  err = co_await read_location(perf_stats, out.perf, out.perf_size);
  if (err.code != 0)
    co_return err;

  co_return points_error_t{};
}

points_error_t object_backend_t::read_index(index_load_t &out)
{
  return run_on_loop_blocking(_event_loop, [this, &out]() { return do_read_index(out); });
}

points_error_t object_backend_t::restore_allocator(const std::unique_ptr<uint8_t[]> &data, uint32_t size)
{
  (void)data;
  (void)size;
  return {}; // object mode has no packed free-list; the id counter comes from the manifest.
}

void object_backend_t::allocate_blob(uint32_t size, storage_location_t &out)
{
  std::unique_lock<std::mutex> lock(_mutex);
  out.file_id = _next_id++;
  out.size = size;
  out.offset = 0;
}

vio::task_t<points_error_t> object_backend_t::write_allocated(storage_location_t location, std::shared_ptr<uint8_t[]> data)
{
  co_return co_await _io->write_object(object_name(location.file_id), std::move(data), location.size);
}

vio::task_t<points_error_t> object_backend_t::read_blob(storage_location_t location, uint8_t *dst, uint32_t &bytes_read)
{
  co_return co_await _io->read_object(object_name(location.file_id), dst, io_range_t{0, int64_t(location.size)}, bytes_read);
}

vio::task_t<points_error_t> object_backend_t::write_index(checkpoint_t checkpoint)
{
  auto alloc_id = [this]() {
    std::unique_lock<std::mutex> lock(_mutex);
    return _next_id++;
  };

  storage_location_t attributes_location{alloc_id(), checkpoint.attribute_configs_size, 0};
  auto err = co_await _io->write_object(object_name(attributes_location.file_id), checkpoint.attribute_configs, attributes_location.size);
  if (err.code != 0)
    co_return err;

  storage_location_t stats_location{alloc_id(), checkpoint.stats_size, 0};
  err = co_await _io->write_object(object_name(stats_location.file_id), checkpoint.stats, stats_location.size);
  if (err.code != 0)
    co_return err;

  storage_location_t perf_location{alloc_id(), checkpoint.perf_size, 0};
  err = co_await _io->write_object(object_name(perf_location.file_id), checkpoint.perf, perf_location.size);
  if (err.code != 0)
    co_return err;

  // The manifest's free-blobs slot carries the next id so allocation resumes correctly on reopen.
  uint32_t next_id_snapshot;
  {
    std::unique_lock<std::mutex> lock(_mutex);
    next_id_snapshot = _next_id;
  }
  storage_location_t free_blobs_slot{0, 0, next_id_snapshot};
  auto index = serialize_index(k_serialized_index_size, free_blobs_slot, attributes_location, checkpoint.tree_registry, stats_location, perf_location);

  // Write the manifest LAST (atomic replace) — the crash-safety commit point.
  err = co_await _io->write_object(k_manifest_name, index, k_serialized_index_size);
  if (err.code != 0)
    co_return err;

  // Commit succeeded: record new metadata locations and only NOW reclaim superseded objects.
  storage_location_t old_attributes = _attributes_location;
  storage_location_t old_stats = _stats_location;
  storage_location_t old_perf = _perf_stats_location;
  _attributes_location = attributes_location;
  _stats_location = stats_location;
  _perf_stats_location = perf_location;
  _tree_registry_location = checkpoint.tree_registry;

  for (auto &loc : checkpoint.freed)
    co_await _io->remove_object(object_name(loc.file_id));
  if (old_attributes.size > 0)
    co_await _io->remove_object(object_name(old_attributes.file_id));
  if (old_stats.size > 0)
    co_await _io->remove_object(object_name(old_stats.file_id));
  if (old_perf.size > 0)
    co_await _io->remove_object(object_name(old_perf.file_id));

  co_return points_error_t{};
}

} // namespace points::converter
