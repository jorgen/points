/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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
#include "storage_handler.hpp"
#include "input_header.hpp"

#include <cassert>
#include <cstring>

#include <algorithm>
#include <limits>
#include <utility>

namespace points::converter
{

void read_request_t::wait_for_read()
{
  std::unique_lock<std::mutex> lock(_mutex);
  _block_for_read.wait(lock, [this] { return this->_done; });
}

storage_handler_t::storage_handler_t(const std::string &url, vio::thread_pool_t &thread_pool, attributes_configs_t &attributes_configs, perf_stats_t &perf_stats, vio::event_pipe_t<void> &index_written,
                                     vio::event_pipe_t<points_error_t> &storage_error_pipe, points_error_t &error)
  : _thread_pool(thread_pool)
  , _event_loop_thread()
  , _event_loop(_event_loop_thread.event_loop())
  , _attributes_configs(attributes_configs)
  , _perf_stats(perf_stats)
  , _index_written(index_written)
  , _storage_error(storage_error_pipe)
  , _write_event_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_write_events))
  , _write_trees_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_write_trees))
  , _write_tree_registry_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_write_tree_registry))
  , _write_blob_locations_and_update_header_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_write_blob_locations_and_update_header))
  , _read_request_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_read_request))
  , _read_cache(256 * 1024 * 1024)
{
  set_compressor(compression_method_t::zstd);
  _backend = create_storage_backend(url, _event_loop, error);
}

storage_handler_t::~storage_handler_t()
{
}

points_error_t storage_handler_t::read_index(std::unique_ptr<uint8_t[]> &free_blobs_buffer, uint32_t &free_blobs_size, std::unique_ptr<uint8_t[]> &attribute_configs_buffer, uint32_t &attribute_configs_size,
                                      std::unique_ptr<uint8_t[]> &tree_registry_buffer, uint32_t &tree_registry_size)
{
  index_load_t load;
  auto error = _backend->read_index(load);
  if (error.code != 0)
    return error;

  free_blobs_buffer = std::move(load.free_blobs);
  free_blobs_size = load.free_blobs_size;
  attribute_configs_buffer = std::move(load.attribute_configs);
  attribute_configs_size = load.attribute_configs_size;
  tree_registry_buffer = std::move(load.tree_registry);
  tree_registry_size = load.tree_registry_size;

  if (load.stats && load.stats_size > 0)
    _compression_stats = compression_stats_t::deserialize(load.stats.get(), load.stats_size);
  if (load.perf && load.perf_size > 0)
    _deserialized_perf_stats = perf_stats_t::deserialize(load.perf.get(), load.perf_size);

  return error;
}

points_error_t storage_handler_t::deserialize_free_blobs(const std::unique_ptr<uint8_t[]> &data, uint32_t size)
{
  return _backend->restore_allocator(data, size);
}

points_error_t storage_handler_t::upgrade_to_write(bool truncate)
{
  auto error = _backend->open_for_write(truncate);
  if (error.code != 0)
  {
    auto error_copy = error;
    _storage_error.post_event(std::move(error));
    return error_copy;
  }
  return {};
}

void storage_handler_t::write(const storage_header_t &header, attributes_id_t attributes_id, attribute_buffers_t &&buffers,
                              std::function<void(const storage_header_t &storageheader, attributes_id_t attrib_id, std::vector<storage_location_t> locations, const points_error_t &error)> done)
{
  _write_event_pipe.post_event(std::make_tuple(header, attributes_id, std::move(buffers), done));
}

void storage_handler_t::write_trees(std::vector<tree_id_t> &&tree_ids, std::vector<serialized_tree_t> &&serialized_trees,
                                    std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, points_error_t &&)> done)
{
  _write_trees_pipe.post_event(std::make_tuple(std::move(tree_ids), std::move(serialized_trees), done));
}

void storage_handler_t::write_tree_registry(serialized_tree_registry_t &&serialized_tree_registry, std::function<void(storage_location_t, points_error_t &&error)> done)
{
  _write_tree_registry_pipe.post_event(std::move(serialized_tree_registry), std::move(done));
}

void storage_handler_t::write_blob_locations_and_update_header(storage_location_t location, std::vector<storage_location_t> &&old_locations, std::function<void(points_error_t &&error)> done)
{
  _write_blob_locations_and_update_header_pipe.post_event(std::move(location), std::move(old_locations), std::move(done));
}

static void compute_attribute_min_max(const uint8_t *data, uint32_t size, const point_format_t &format, double &out_min, double &out_max)
{
  out_min = std::numeric_limits<double>::max();
  out_max = std::numeric_limits<double>::lowest();
  int elem_size = size_for_format(format.type) * static_cast<int>(format.components);
  if (elem_size <= 0 || size == 0)
    return;
  uint32_t count = size / static_cast<uint32_t>(elem_size);
  if (count == 0)
    return;
  for (uint32_t i = 0; i < count; i++)
  {
    const uint8_t *elem = data + i * elem_size;
    double val = 0.0;
    switch (format.type)
    {
    case points_type_u8: { uint8_t v; memcpy(&v, elem, 1); val = double(v); break; }
    case points_type_i8: { int8_t v; memcpy(&v, elem, 1); val = double(v); break; }
    case points_type_u16: { uint16_t v; memcpy(&v, elem, 2); val = double(v); break; }
    case points_type_i16: { int16_t v; memcpy(&v, elem, 2); val = double(v); break; }
    case points_type_u32: { uint32_t v; memcpy(&v, elem, 4); val = double(v); break; }
    case points_type_i32: { int32_t v; memcpy(&v, elem, 4); val = double(v); break; }
    case points_type_r32: { float v; memcpy(&v, elem, 4); val = double(v); break; }
    case points_type_u64: { uint64_t v; memcpy(&v, elem, 8); val = double(v); break; }
    case points_type_i64: { int64_t v; memcpy(&v, elem, 8); val = double(v); break; }
    case points_type_r64: { double v; memcpy(&v, elem, 8); val = v; break; }
    default: continue;
    }
    if (val < out_min) out_min = val;
    if (val > out_max) out_max = val;
  }
}

static bool serialize_points(const storage_header_t &header, const points_converter_buffer_t &points, points_converter_buffer_t &serialize_data, std::shared_ptr<uint8_t[]> &data_owner)
{
  serialize_data.size = sizeof(header) + points.size;
  data_owner = std::make_shared<uint8_t[]>(serialize_data.size);
  serialize_data.data = data_owner.get();
  auto output_bytes = static_cast<uint8_t *>(serialize_data.data);
  memcpy(output_bytes, &header, sizeof(header));
  memcpy(output_bytes + sizeof(header), points.data, points.size);
  return true;
}

vio::task_t<void> storage_handler_t::do_write(const std::shared_ptr<uint8_t[]> &data, const storage_location_t &location)
{
  assert(location.size > 0);
  assert(data != nullptr);
  // The blob manager reuses freed offsets, so an offset previously read (and cached) may now
  // hold different data. Invalidate the stale read-cache entry for this (file_id, offset) so a
  // later read does not return the old blob's bytes.
  _read_cache.erase(cache_key_t{location.file_id, location.offset});
  auto error = co_await _backend->write_allocated(location, data);
  if (error.code != 0)
  {
    _storage_error.post_event(std::move(error));
  }
}

vio::task_t<void> storage_handler_t::do_write_events(storage_header_t header, attributes_id_t attributes_id, attribute_buffers_t attribute_buffers,
                                                     std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const points_error_t &error)> done)
{
  auto write_start = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(_mutex);

  bool is_leaf = input_data_id_is_leaf(header.input_id);
  if (is_leaf)
    _seen_input_files.insert(header.input_id.data);

  uint64_t uncompressed_total = 0;
  for (auto &buf : attribute_buffers.buffers)
    uncompressed_total += buf.size;

  auto formats = _attributes_configs.get_format_components(attributes_id);
  auto &attributes = _attributes_configs.get(attributes_id);
  int buffer_count = int(attribute_buffers.buffers.size());

  // Prepare per-buffer data for compression
  struct buffer_info_t
  {
    std::shared_ptr<uint8_t[]> data_owner;
    uint8_t *raw;
    uint32_t size;
    point_format_t format;
    std::string attr_name;
    bool is_lod;
  };
  std::vector<buffer_info_t> buffer_infos(buffer_count);

  bool is_lod = !is_leaf;

  for (int i = 0; i < buffer_count; i++)
  {
    auto &info = buffer_infos[i];
    if (i == 0)
    {
      points_converter_buffer_t buffer_data;
      serialize_points(header, attribute_buffers.buffers[i], buffer_data, info.data_owner);
      info.raw = static_cast<uint8_t *>(buffer_data.data);
      info.size = buffer_data.size;
      info.format = header.point_format;
    }
    else
    {
      info.raw = static_cast<uint8_t *>(attribute_buffers.buffers[i].data);
      info.size = attribute_buffers.buffers[i].size;
      info.data_owner = std::move(attribute_buffers.data[i]);
      if (i < int(formats.size()))
        info.format = formats[i];
      else
        info.format = {points_type_u8, points_components_1};
    }

    if (i < int(attributes.attributes.size()))
      info.attr_name = std::string(attributes.attributes[i].name, attributes.attributes[i].name_size);
    else
      info.attr_name = "unknown";
    info.is_lod = is_lod;
  }

  lock.unlock();

  std::vector<storage_location_t> locations(buffer_count);
  points_error_t error;

  if (_compressor)
  {
    auto *compressor = _compressor.get();
    uint32_t point_count = header.point_count;

    // Build work items for schedule_work
    std::vector<std::function<std::expected<compressed_write_data_t, vio::error_t>()>> work_items;
    work_items.reserve(buffer_count);

    for (int i = 0; i < buffer_count; i++)
    {
      auto &info = buffer_infos[i];
      work_items.push_back([compressor, raw = info.raw, size = info.size, data_owner = info.data_owner,
                            format = info.format, point_count, i, attr_name = info.attr_name, is_lod = info.is_lod]() -> std::expected<compressed_write_data_t, vio::error_t>
      {
        double attr_min = std::numeric_limits<double>::max();
        double attr_max = std::numeric_limits<double>::lowest();
        if (i > 0)
        {
          compute_attribute_min_max(raw, size, format, attr_min, attr_max);
        }
        auto compressed = try_compress_constant(raw, size, format);
        if (!compressed.data)
          compressed = compressor->compress(raw, size, format, point_count);

        compressed_write_data_t wd;
        wd.buffer_index = i;
        wd.attribute_name = attr_name;
        wd.format = format;
        wd.uncompressed_size = size;
        wd.min_value = attr_min;
        wd.max_value = attr_max;
        wd.is_lod = is_lod;

        if (compressed.error.code != 0)
        {
          wd.data = data_owner;
          wd.size = size;
        }
        else
        {
          wd.data = std::move(compressed.data);
          wd.size = compressed.size;
        }
        return wd;
      });
    }

    auto results = co_await vio::schedule_work(_event_loop, _thread_pool, std::move(work_items));

    // Process compression results on the event loop thread
    for (auto &result : results)
    {
      if (!result.has_value())
      {
        error.code = result.error().code;
        error.msg = result.error().msg;
        continue;
      }
      auto &wd = result.value();

      uint8_t compression_flags = 0;
      if (wd.data && wd.size >= sizeof(compression_header_t) && has_compression_magic(wd.data.get(), wd.size))
      {
        compression_header_t hdr;
        memcpy(&hdr, wd.data.get(), sizeof(hdr));
        compression_flags = hdr.flags;
      }
      _compression_stats.accumulate(wd.attribute_name, wd.format, wd.uncompressed_size, wd.size, wd.min_value, wd.max_value, compression_flags, wd.is_lod);

      auto &location = locations[wd.buffer_index];
      _backend->allocate_blob(wd.size, location);

      co_await do_write(wd.data, location);
    }
  }
  else
  {
    for (int i = 0; i < buffer_count; i++)
    {
      auto &info = buffer_infos[i];
      _compression_stats.accumulate(info.attr_name, info.format, info.size, info.size, std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), 0, info.is_lod);

      auto &location = locations[i];
      _backend->allocate_blob(info.size, location);

      co_await do_write(info.data_owner, location);
    }
  }

  auto write_end = std::chrono::steady_clock::now();
  auto write_us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(write_end - write_start).count());
  if (is_leaf)
    _perf_stats.source_write.record(uncompressed_total, write_us);
  else
    _perf_stats.lod_write.record(uncompressed_total, write_us);
  if (_on_write_progress)
    _on_write_progress();

  if (done)
  {
    done(header, attributes_id, std::move(locations), error);
  }
}

void storage_handler_t::handle_write_events(
  std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const points_error_t &error)>> &&event)
{
  auto &&[storage_header, attributes_id, attribute_buffers, done] = std::move(event);
  [](storage_handler_t *self, storage_header_t header, attributes_id_t attrib_id, attribute_buffers_t buffers,
     std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const points_error_t &error)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_events(std::move(header), std::move(attrib_id), std::move(buffers), std::move(done_cb));
  }(this, std::move(storage_header), std::move(attributes_id), std::move(attribute_buffers), std::move(done));
}

vio::task_t<void> storage_handler_t::do_write_trees(std::vector<tree_id_t> tree_ids, std::vector<serialized_tree_t> serialized_trees,
                                                    std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, points_error_t &&)> done)
{
  std::unique_lock<std::mutex> lock(_mutex);
  std::vector<storage_location_t> locations(tree_ids.size());
  points_error_t error;

  for (int i = 0; i < int(tree_ids.size()); i++)
  {
    _backend->allocate_blob(serialized_trees[i].size, locations[i]);
  }
  lock.unlock();

  for (int i = 0; i < int(tree_ids.size()); i++)
  {
    auto result = co_await _backend->write_allocated(locations[i], serialized_trees[i].data);
    if (result.code != 0 && error.code == 0)
    {
      error = result;
    }
  }

  if (done)
  {
    done(std::move(tree_ids), std::move(locations), std::move(error));
  }
}

void storage_handler_t::handle_write_trees(std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, points_error_t &&)>> &&event)
{
  auto &&[tree_ids, serialized_trees, done] = std::move(event);
  [](storage_handler_t *self, std::vector<tree_id_t> ids, std::vector<serialized_tree_t> trees,
     std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, points_error_t &&)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_trees(std::move(ids), std::move(trees), std::move(done_cb));
  }(this, std::move(tree_ids), std::move(serialized_trees), std::move(done));
}

vio::task_t<void> storage_handler_t::do_write_tree_registry(serialized_tree_registry_t serialized_tree_registry, std::function<void(storage_location_t, points_error_t &&error)> done)
{
  std::unique_lock<std::mutex> lock(_mutex);
  storage_location_t location;
  _backend->allocate_blob(uint32_t(serialized_tree_registry.size), location);
  lock.unlock();

  points_error_t error = co_await _backend->write_allocated(location, serialized_tree_registry.data);

  if (done)
  {
    done(location, std::move(error));
  }
}

void storage_handler_t::handle_write_tree_registry(serialized_tree_registry_t &&serialized_tree, std::function<void(storage_location_t, points_error_t &&error)> &&done)
{
  [](storage_handler_t *self, serialized_tree_registry_t reg, std::function<void(storage_location_t, points_error_t &&error)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_tree_registry(std::move(reg), std::move(done_cb));
  }(this, std::move(serialized_tree), std::move(done));
}

vio::task_t<void> storage_handler_t::do_write_blob_locations_and_update_header(storage_location_t new_tree_registry_location, std::vector<storage_location_t> old_locations, std::function<void(points_error_t &&error)> done)
{
  auto serialized_attributes_configs = _attributes_configs.serialize();

  _compression_stats.input_file_count = static_cast<uint32_t>(_seen_input_files.size());
  _compression_stats.input_file_size_bytes = 0;
  for (auto &[id, size] : _input_file_sizes)
    _compression_stats.input_file_size_bytes += size;
  if (_compressor)
    _compression_stats.method = _compressor->method();

  uint32_t stats_size = 0;
  auto serialized_stats_data = _compression_stats.serialize(stats_size);

  uint32_t perf_size = 0;
  auto serialized_perf_data = _perf_stats.serialize(perf_size);

  checkpoint_t checkpoint;
  checkpoint.tree_registry = new_tree_registry_location;
  checkpoint.freed = std::move(old_locations);
  checkpoint.attribute_configs = serialized_attributes_configs.data;
  checkpoint.attribute_configs_size = serialized_attributes_configs.size;
  checkpoint.stats = serialized_stats_data;
  checkpoint.stats_size = stats_size;
  checkpoint.perf = std::move(serialized_perf_data);
  checkpoint.perf_size = perf_size;

  // The backend writes the metadata blobs, then the index/manifest LAST, fsyncs, commits, and only
  // then reclaims the freed blobs. The index-written event fires only after that whole barrier succeeds.
  auto error = co_await _backend->write_index(std::move(checkpoint));
  if (error.code != 0)
  {
    done(std::move(error));
    co_return;
  }
  _index_written.post_event();
  done(points_error_t{});
}

void storage_handler_t::handle_write_blob_locations_and_update_header(storage_location_t &&new_tree_registry_location, std::vector<storage_location_t> &&old_locations, std::function<void(points_error_t &&error)> &&done)
{
  [](storage_handler_t *self, storage_location_t loc, std::vector<storage_location_t> old_locs,
     std::function<void(points_error_t &&error)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_blob_locations_and_update_header(std::move(loc), std::move(old_locs), std::move(done_cb));
  }(this, std::move(new_tree_registry_location), std::move(old_locations), std::move(done));
}

void storage_handler_t::register_input_file_size(uint32_t file_id, uint64_t size_bytes)
{
  std::unique_lock<std::mutex> lock(_mutex);
  _input_file_sizes[file_id] = size_bytes;
}

void storage_handler_t::set_compressor(compression_method_t method)
{
  _compressor = create_compressor(method);
}

void storage_handler_t::set_read_cache_size(uint64_t max_bytes)
{
  _read_cache.clear();
  _read_cache.set_max_bytes(max_bytes);
}

std::shared_ptr<read_request_t> storage_handler_t::read(storage_location_t location)
{
  auto ret = std::make_shared<read_request_t>();

  cache_key_t key{location.file_id, location.offset};
  auto cached = _read_cache.get(key);
  if (cached.has_value())
  {
    _perf_stats.cache_hits.fetch_add(1, std::memory_order_relaxed);
    auto &cv = cached.value();
    if (has_compression_magic(cv.compressed_data.get(), cv.compressed_size))
    {
      auto decompressed = decompress_any(cv.compressed_data.get(), cv.compressed_size);
      if (decompressed.error.code == 0)
      {
        ret->buffer = std::move(decompressed.data);
        ret->buffer_info.data = ret->buffer.get();
        ret->buffer_info.size = decompressed.size;
      }
      else
      {
        ret->error = std::move(decompressed.error);
      }
    }
    else
    {
      ret->buffer = cv.compressed_data;
      ret->buffer_info.data = ret->buffer.get();
      ret->buffer_info.size = cv.compressed_size;
    }
    std::unique_lock<std::mutex> lock(ret->_mutex);
    ret->_done = true;
    ret->_block_for_read.notify_all();
    return ret;
  }

  _perf_stats.cache_misses.fetch_add(1, std::memory_order_relaxed);
  auto copy = ret;
  _read_request_pipe.post_event(std::move(copy), std::move(location));
  return ret;
}

void storage_handler_t::handle_read_request(std::shared_ptr<read_request_t> &&read_request, storage_location_t &&location)
{
  [](storage_handler_t *self, std::shared_ptr<read_request_t> req, storage_location_t loc) -> vio::detached_task_t
  {
    co_await self->do_read_request(std::move(req), std::move(loc));
  }(this, std::move(read_request), std::move(location));
}

vio::task_t<void> storage_handler_t::do_read_request(std::shared_ptr<read_request_t> read_request, storage_location_t location)
{
  auto read_start = std::chrono::steady_clock::now();
  auto buffer = std::make_shared<uint8_t[]>(location.size);
  uint32_t bytes_read = 0;
  auto result = co_await _backend->read_blob(location, buffer.get(), bytes_read);

  if (result.code != 0)
  {
    read_request->error = std::move(result);
  }
  else
  {
    // Cache the raw compressed data before decompression
    cache_key_t key{location.file_id, location.offset};
    _read_cache.put(key, cache_value_t{buffer, location.size}, location.size);

    read_request->buffer = buffer;
    read_request->buffer_info.size = bytes_read;
    read_request->buffer_info.data = buffer.get();

    // Decompress if needed
    if (read_request->buffer && has_compression_magic(read_request->buffer.get(), read_request->buffer_info.size))
    {
      auto decompressed = decompress_any(read_request->buffer.get(), read_request->buffer_info.size);
      if (decompressed.error.code == 0)
      {
        read_request->buffer = std::move(decompressed.data);
        read_request->buffer_info.data = read_request->buffer.get();
        read_request->buffer_info.size = decompressed.size;
      }
      else
      {
        read_request->error = std::move(decompressed.error);
      }
    }
  }

  auto read_end = std::chrono::steady_clock::now();
  auto read_us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(read_end - read_start).count());
  _perf_stats.lod_read.record(location.size, read_us);

  std::unique_lock<std::mutex> lock(read_request->_mutex);
  read_request->_done = true;
  read_request->_block_for_read.notify_all();
}

} // namespace points::converter
