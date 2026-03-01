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

#include <uv.h>

#include <fmt/printf.h>

#include <cassert>
#include <cstring>
#include <fcntl.h>

#include <algorithm>
#include <limits>
#include <utility>

#include <fmt/format.h>

namespace points::converter
{

static std::shared_ptr<uint8_t[]> serialize_index(const uint32_t index_size, const storage_location_t &free_blobs, const storage_location_t &attribute_configs, const storage_location_t &tree_registry,
                                                   const storage_location_t &compression_stats)
{
  auto serialized_index = std::make_shared<uint8_t[]>(index_size);
  auto *data = serialized_index.get();
  memset(data, 0, index_size);

  uint8_t magic[] = {'J', 'L', 'P', 0};
  memcpy(data, magic, sizeof(magic));
  data += sizeof(magic);

  memcpy(data, &free_blobs, sizeof(free_blobs));
  data += sizeof(free_blobs);

  memcpy(data, &attribute_configs, sizeof(attribute_configs));
  data += sizeof(attribute_configs);

  memcpy(data, &tree_registry, sizeof(tree_registry));
  data += sizeof(tree_registry);

  memcpy(data, &compression_stats, sizeof(compression_stats));

  return serialized_index;
}
[[nodiscard]] static error_t deserialize_index(const uint8_t *buffer, uint32_t buffer_size, storage_location_t &free_blobs, storage_location_t &attribute_configs, storage_location_t &tree_registry,
                                               storage_location_t &compression_stats)
{
  (void)buffer_size; // buffer size is validated by the caller
  uint8_t magic[] = {'J', 'L', 'P', 0};
  if (memcmp(buffer, magic, sizeof(magic)) != 0)
  {
    error_t ret;
    ret.code = 1;
    ret.msg = "Wrong magic.";
    return ret;
  }
  auto ptr = buffer + sizeof(magic);
  memcpy(&free_blobs, ptr, sizeof(free_blobs));
  ptr += sizeof(free_blobs);

  memcpy(&attribute_configs, ptr, sizeof(attribute_configs));
  ptr += sizeof(attribute_configs);

  memcpy(&tree_registry, ptr, sizeof(tree_registry));
  ptr += sizeof(tree_registry);

  memcpy(&compression_stats, ptr, sizeof(compression_stats));
  return {};
}

void read_request_t::wait_for_read()
{
  std::unique_lock<std::mutex> lock(_mutex);
  _block_for_read.wait(lock, [this] { return this->_done; });
}

static std::unique_ptr<uint8_t[]> read_into_buffer(vio::event_loop_t &event_loop, uv_file file_handle, uv_fs_t &request, const storage_location_t &location, error_t &error)
{
  assert(error.code == 0);
  auto buffer = std::make_unique<uint8_t[]>(location.size);
  uv_buf_t uv_buffer;
  uv_buffer.base = (char *)buffer.get();
  uv_buffer.len = location.size;
  auto result = uv_fs_read(event_loop.loop(), &request, file_handle, &uv_buffer, 1, int64_t(location.offset), NULL);
  if (result < 0 || request.result != location.size)
  {
    error.code = 1;
    error.msg = "Could not read the entire buffer";
    return nullptr;
  }
  return buffer;
}

storage_handler_t::storage_handler_t(const std::string &url, vio::thread_pool_t &thread_pool, attributes_configs_t &attributes_configs, vio::event_pipe_t<void> &index_written,
                                     vio::event_pipe_t<error_t> &storage_error_pipe, error_t &error)
  : _file_name(url)
  , _thread_pool(thread_pool)
  , _event_loop_thread()
  , _event_loop(_event_loop_thread.event_loop())
  , _file_opened(false)
  , _file_exists(false)
  , _file_opened_in_write_mode(false)
  , _attributes_configs(attributes_configs)
  , _serialized_index_size(128)
  , _index_written(index_written)
  , _storage_error(storage_error_pipe)
  , _write_event_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_write_events))
  , _write_trees_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_write_trees))
  , _write_tree_registry_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_write_tree_registry))
  , _write_blob_locations_and_update_header_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_write_blob_locations_and_update_header))
  , _read_request_pipe(_event_loop, vio::event_bind_t::bind(*this, &storage_handler_t::handle_read_request))
{
  set_compressor(compression_method_t::blosc2);
  auto stat_result = vio::stat_file(_event_loop, _file_name);
  if (!stat_result.has_value())
  {
    auto index = _blob_manager.register_blob({_serialized_index_size});
    assert(index.data == 0);
    return;
  }
  _file_exists = true;
  auto open_result = vio::open_file(_event_loop, _file_name, vio::file_open_flags_t(vio::file_open_flag_t::rdonly), 0);
  if (!open_result.has_value())
  {
    error.code = open_result.error().code;
    error.msg = open_result.error().msg;
    return;
  }
  _file = std::move(open_result.value());
  _file_opened = true;
}

storage_handler_t::~storage_handler_t()
{
  _file.reset();
}

error_t storage_handler_t::read_index(std::unique_ptr<uint8_t[]> &free_blobs_buffer, uint32_t &free_blobs_size, std::unique_ptr<uint8_t[]> &attribute_configs_buffer, uint32_t &attribute_configs_size,
                                      std::unique_ptr<uint8_t[]> &tree_registry_buffer, uint32_t &tree_registry_size)
{
  error_t error;
  uv_fs_t request = {};
  // On error, close the file
  struct close_on_error_t
  {
    std::optional<vio::auto_close_file_t> &file;
    error_t &error;
    ~close_on_error_t()
    {
      if (error.code != 0)
      {
        file.reset();
      }
    }
  } closer{_file, error};

  auto &file = **_file;
  auto index_buffer = std::make_unique<uint8_t[]>(_serialized_index_size);
  uv_buf_t index_uv_buf;
  index_uv_buf.base = (char *)index_buffer.get();
  index_uv_buf.len = _serialized_index_size;
  auto read = uv_fs_read(_event_loop.loop(), &request, file.handle, &index_uv_buf, 1, 0, NULL);
  if (read < 0)
  {
    error.code = 1;
    error.msg = uv_strerror(read);
    return error;
  }
  if (uint32_t(read) != _serialized_index_size)
  {
    error.code = 1;
    error.msg = "could not read index";
    return error;
  }
  storage_location_t free_blobs;
  storage_location_t attribute_configs;
  storage_location_t tree_registry;
  storage_location_t compression_stats;
  error = deserialize_index(index_buffer.get(), _serialized_index_size, free_blobs, attribute_configs, tree_registry, compression_stats);
  if (error.code != 0)
  {
    return error;
  }

  free_blobs_buffer = read_into_buffer(_event_loop, file.handle, request, free_blobs, error);
  free_blobs_size = free_blobs.size;
  if (!free_blobs_buffer)
  {
    error.code = 1;
    error.msg = "Failed to read free blobs: " + error.msg;
    return error;
  }

  attribute_configs_buffer = read_into_buffer(_event_loop, file.handle, request, attribute_configs, error);
  attribute_configs_size = attribute_configs.size;
  if (!attribute_configs_buffer)
  {
    error.code = 1;
    error.msg = "Failed to read attribute_configs: " + error.msg;
    return error;
  }

  tree_registry_buffer = read_into_buffer(_event_loop, file.handle, request, tree_registry, error);
  tree_registry_size = tree_registry.size;
  if (!tree_registry_buffer)
  {
    error.code = 1;
    error.msg = "Failed to read tree_registry: " + error.msg;
    return error;
  }
  return error;
}

error_t storage_handler_t::deserialize_free_blobs(const std::unique_ptr<uint8_t[]> &data, uint32_t size)
{
  return _blob_manager.deserialize(data, size);
}

error_t storage_handler_t::upgrade_to_write(bool truncate)
{
  vio::file_open_flags_t open_flags(vio::file_open_flag_t::rdwr);
  if (!_file_exists)
  {
    open_flags |= vio::file_open_flag_t::creat;
  }

  _file.reset();

#ifdef WIN32
  int open_mode = _S_IREAD | _S_IWRITE;
#else
  int open_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
#endif

  auto open_result = vio::open_file(_event_loop, _file_name, open_flags, open_mode);
  if (!open_result.has_value())
  {
    error_t error;
    error.code = open_result.error().code;
    error.msg = open_result.error().msg;
    auto error_copy = error;
    _storage_error.post_event(std::move(error));
    return error_copy;
  }
  _file = std::move(open_result.value());

  if (truncate)
  {
    uv_fs_t request = {};
    uv_fs_ftruncate(_event_loop.loop(), &request, (**_file).handle, 0, NULL);
  }

  return {};
}

void storage_handler_t::write(const storage_header_t &header, attributes_id_t attributes_id, attribute_buffers_t &&buffers,
                              std::function<void(const storage_header_t &storageheader, attributes_id_t attrib_id, std::vector<storage_location_t> locations, const error_t &error)> done)
{
  _write_event_pipe.post_event(std::make_tuple(header, attributes_id, std::move(buffers), done));
}

void storage_handler_t::write_trees(std::vector<tree_id_t> &&tree_ids, std::vector<serialized_tree_t> &&serialized_trees,
                                    std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&)> done)
{
  _write_trees_pipe.post_event(std::make_tuple(std::move(tree_ids), std::move(serialized_trees), done));
}

void storage_handler_t::write_tree_registry(serialized_tree_registry_t &&serialized_tree_registry, std::function<void(storage_location_t, error_t &&error)> done)
{
  _write_tree_registry_pipe.post_event(std::move(serialized_tree_registry), std::move(done));
}

void storage_handler_t::write_blob_locations_and_update_header(storage_location_t location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> done)
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
    case type_u8: { uint8_t v; memcpy(&v, elem, 1); val = double(v); break; }
    case type_i8: { int8_t v; memcpy(&v, elem, 1); val = double(v); break; }
    case type_u16: { uint16_t v; memcpy(&v, elem, 2); val = double(v); break; }
    case type_i16: { int16_t v; memcpy(&v, elem, 2); val = double(v); break; }
    case type_u32: { uint32_t v; memcpy(&v, elem, 4); val = double(v); break; }
    case type_i32: { int32_t v; memcpy(&v, elem, 4); val = double(v); break; }
    case type_r32: { float v; memcpy(&v, elem, 4); val = double(v); break; }
    case type_u64: { uint64_t v; memcpy(&v, elem, 8); val = double(v); break; }
    case type_i64: { int64_t v; memcpy(&v, elem, 8); val = double(v); break; }
    case type_r64: { double v; memcpy(&v, elem, 8); val = v; break; }
    default: continue;
    }
    if (val < out_min) out_min = val;
    if (val > out_max) out_max = val;
  }
}

static bool serialize_points(const storage_header_t &header, const buffer_t &points, buffer_t &serialize_data, std::shared_ptr<uint8_t[]> &data_owner)
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
  auto &file = **_file;
  auto result = co_await vio::write_file(_event_loop, file, data.get(), location.size, int64_t(location.offset));
  if (!result.has_value())
  {
    fprintf(stderr, "Write error %d - %s\n", result.error().code, result.error().msg.c_str());
  }
}

vio::task_t<void> storage_handler_t::do_write_events(storage_header_t header, attributes_id_t attributes_id, attribute_buffers_t attribute_buffers,
                                                     std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)> done)
{
  std::unique_lock<std::mutex> lock(_mutex);

  if (input_data_id_is_leaf(header.input_id))
    _seen_input_files.insert(header.input_id.data);

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

  bool is_lod = !input_data_id_is_leaf(header.input_id);

  for (int i = 0; i < buffer_count; i++)
  {
    auto &info = buffer_infos[i];
    if (i == 0)
    {
      buffer_t buffer_data;
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
        info.format = {type_u8, components_1};
    }

    if (i < int(attributes.attributes.size()))
      info.attr_name = std::string(attributes.attributes[i].name, attributes.attributes[i].name_size);
    else
      info.attr_name = "unknown";
    info.is_lod = is_lod;
  }

  lock.unlock();

  std::vector<storage_location_t> locations(buffer_count);
  error_t error;

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
      location.file_id = 0;
      location.size = wd.size;
      free_blob_manager_t::blob_size_t blob_size = {location.size};
      location.offset = _blob_manager.register_blob(blob_size).data;

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
      location.file_id = 0;
      location.size = info.size;
      free_blob_manager_t::blob_size_t size = {location.size};
      location.offset = _blob_manager.register_blob(size).data;

      co_await do_write(info.data_owner, location);
    }
  }

  if (done)
  {
    done(header, attributes_id, std::move(locations), error);
  }
}

void storage_handler_t::handle_write_events(
  std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)>> &&event)
{
  auto &&[storage_header, attributes_id, attribute_buffers, done] = std::move(event);
  [](storage_handler_t *self, storage_header_t header, attributes_id_t attrib_id, attribute_buffers_t buffers,
     std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_events(std::move(header), std::move(attrib_id), std::move(buffers), std::move(done_cb));
  }(this, std::move(storage_header), std::move(attributes_id), std::move(attribute_buffers), std::move(done));
}

vio::task_t<void> storage_handler_t::do_write_trees(std::vector<tree_id_t> tree_ids, std::vector<serialized_tree_t> serialized_trees,
                                                    std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&)> done)
{
  std::unique_lock<std::mutex> lock(_mutex);
  std::vector<storage_location_t> locations(tree_ids.size());
  error_t error;

  for (int i = 0; i < int(tree_ids.size()); i++)
  {
    auto &location = locations[i];
    location.file_id = 0;
    location.size = serialized_trees[i].size;
    free_blob_manager_t::blob_size_t size = {location.size};
    location.offset = _blob_manager.register_blob(size).data;
  }
  lock.unlock();

  auto &file = **_file;
  for (int i = 0; i < int(tree_ids.size()); i++)
  {
    auto result = co_await vio::write_file(_event_loop, file, reinterpret_cast<const uint8_t *>(serialized_trees[i].data.get()), locations[i].size, int64_t(locations[i].offset));
    if (!result.has_value() && error.code == 0)
    {
      error.code = result.error().code;
      error.msg = result.error().msg;
    }
  }

  if (done)
  {
    done(std::move(tree_ids), std::move(locations), std::move(error));
  }
}

void storage_handler_t::handle_write_trees(std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&)>> &&event)
{
  auto &&[tree_ids, serialized_trees, done] = std::move(event);
  [](storage_handler_t *self, std::vector<tree_id_t> ids, std::vector<serialized_tree_t> trees,
     std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_trees(std::move(ids), std::move(trees), std::move(done_cb));
  }(this, std::move(tree_ids), std::move(serialized_trees), std::move(done));
}

vio::task_t<void> storage_handler_t::do_write_tree_registry(serialized_tree_registry_t serialized_tree_registry, std::function<void(storage_location_t, error_t &&error)> done)
{
  std::unique_lock<std::mutex> lock(_mutex);
  storage_location_t location;
  location.file_id = 0;
  location.size = serialized_tree_registry.size;
  free_blob_manager_t::blob_size_t size = {location.size};
  location.offset = _blob_manager.register_blob(size).data;
  lock.unlock();

  auto &file = **_file;
  error_t error;
  auto result = co_await vio::write_file(_event_loop, file, reinterpret_cast<const uint8_t *>(serialized_tree_registry.data.get()), location.size, int64_t(location.offset));
  if (!result.has_value())
  {
    error.code = result.error().code;
    error.msg = result.error().msg;
  }

  if (done)
  {
    done(location, std::move(error));
  }
}

void storage_handler_t::handle_write_tree_registry(serialized_tree_registry_t &&serialized_tree, std::function<void(storage_location_t, error_t &&error)> &&done)
{
  [](storage_handler_t *self, serialized_tree_registry_t reg, std::function<void(storage_location_t, error_t &&error)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_tree_registry(std::move(reg), std::move(done_cb));
  }(this, std::move(serialized_tree), std::move(done));
}

vio::task_t<void> storage_handler_t::do_write_blob_locations_and_update_header(storage_location_t new_tree_registry_location, std::vector<storage_location_t> old_locations, std::function<void(error_t &&error)> done)
{
  auto new_blob_manager = _blob_manager;
  for (auto &location : old_locations)
  {
    auto removed = new_blob_manager.unregister_blob({location.offset}, {location.size});
    if (!removed)
    {
      error_t error;
      error.code = -1;
      error.msg = "Failed to remove blob";
      done(std::move(error));
      co_return;
    }
  }
  if (_attributes_location.offset > 0)
  {
    auto removed = new_blob_manager.unregister_blob({_attributes_location.offset}, {_attributes_location.size});
    if (!removed)
    {
      error_t error;
      error.code = -1;
      error.msg = "Failed to remove attributes config location";
      done(std::move(error));
      co_return;
    }
  }

  if (_blobs_location.offset > 0)
  {
    auto removed = new_blob_manager.unregister_blob({_blobs_location.offset}, {_blobs_location.size});
    if (!removed)
    {
      error_t error;
      error.code = -1;
      error.msg = "Failed to remove blobs location";
      done(std::move(error));
      co_return;
    }
  }

  if (_stats_location.offset > 0)
  {
    auto removed = new_blob_manager.unregister_blob({_stats_location.offset}, {_stats_location.size});
    if (!removed)
    {
      error_t error;
      error.code = -1;
      error.msg = "Failed to remove stats location";
      done(std::move(error));
      co_return;
    }
  }

  auto serialized_attributes_configs = _attributes_configs.serialize();
  storage_location_t serialized_attributes_configs_location;
  serialized_attributes_configs_location.offset = new_blob_manager.register_blob({serialized_attributes_configs.size}).data;
  serialized_attributes_configs_location.size = serialized_attributes_configs.size;

  _compression_stats.input_file_count = static_cast<uint32_t>(_seen_input_files.size());
  if (_compressor)
    _compression_stats.method = _compressor->method();

  uint32_t stats_size = 0;
  auto serialized_stats_data = _compression_stats.serialize(stats_size);
  storage_location_t serialized_stats_location;
  serialized_stats_location.offset = new_blob_manager.register_blob({stats_size}).data;
  serialized_stats_location.size = stats_size;

  auto serialized_blob = new_blob_manager.serialize();

  storage_location_t serialized_blob_location = {0, serialized_blob.size, serialized_blob.offset};

  // Write all three blobs
  auto &file = **_file;
  auto result1 = co_await vio::write_file(_event_loop, file, reinterpret_cast<const uint8_t *>(serialized_blob.data.get()), serialized_blob.size, int64_t(serialized_blob.offset));
  if (!result1.has_value())
  {
    error_t error;
    error.code = result1.error().code;
    error.msg = result1.error().msg;
    done(std::move(error));
    co_return;
  }

  auto result2 = co_await vio::write_file(_event_loop, file, reinterpret_cast<const uint8_t *>(serialized_attributes_configs.data.get()), serialized_attributes_configs_location.size, int64_t(serialized_attributes_configs_location.offset));
  if (!result2.has_value())
  {
    error_t error;
    error.code = result2.error().code;
    error.msg = result2.error().msg;
    done(std::move(error));
    co_return;
  }

  auto result3 = co_await vio::write_file(_event_loop, file, serialized_stats_data.get(), serialized_stats_location.size, int64_t(serialized_stats_location.offset));
  if (!result3.has_value())
  {
    error_t error;
    error.code = result3.error().code;
    error.msg = result3.error().msg;
    done(std::move(error));
    co_return;
  }

  co_await do_write_index(std::move(new_blob_manager), serialized_blob_location, serialized_attributes_configs_location, new_tree_registry_location,
                          serialized_stats_location, std::move(done));
}

void storage_handler_t::handle_write_blob_locations_and_update_header(storage_location_t &&new_tree_registry_location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> &&done)
{
  [](storage_handler_t *self, storage_location_t loc, std::vector<storage_location_t> old_locs,
     std::function<void(error_t &&error)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_blob_locations_and_update_header(std::move(loc), std::move(old_locs), std::move(done_cb));
  }(this, std::move(new_tree_registry_location), std::move(old_locations), std::move(done));
}

vio::task_t<void> storage_handler_t::do_write_index(free_blob_manager_t new_blob_manager, storage_location_t free_blobs, storage_location_t attribute_configs, storage_location_t tree_registry,
                                                    storage_location_t compression_stats, std::function<void(error_t &&error)> done)
{
  auto serialized_index = serialize_index(_serialized_index_size, free_blobs, attribute_configs, tree_registry, compression_stats);

  fmt::print(stderr, "Writing index:\n  free_blobs: {}\n  attribute_configs: {}\n  tree_registry: {}\n", free_blobs, attribute_configs, tree_registry);

  auto &file = **_file;
  auto result = co_await vio::write_file(_event_loop, file, serialized_index.get(), _serialized_index_size, 0);

  error_t error;
  fmt::print(stderr, "Write index done {}\n", result.has_value() ? 0 : result.error().code);
  if (!result.has_value())
  {
    error.code = result.error().code;
    error.msg = result.error().msg;
    done(std::move(error));
    co_return;
  }

  _blob_manager = std::move(new_blob_manager);
  _blobs_location = free_blobs;
  _attributes_location = attribute_configs;
  _stats_location = compression_stats;

  uv_fs_t req = {};
  uv_fs_fsync(_event_loop.loop(), &req, file.handle, NULL);
  _index_written.post_event();
  done(std::move(error));
}

void storage_handler_t::handle_write_index(free_blob_manager_t &&new_blob_manager, const storage_location_t &free_blobs, const storage_location_t &attribute_configs, const storage_location_t &tree_registry,
                                           const storage_location_t &compression_stats, std::function<void(error_t &&error)> &&done)
{
  [](storage_handler_t *self, free_blob_manager_t bm, storage_location_t fb, storage_location_t ac, storage_location_t tr,
     storage_location_t cs, std::function<void(error_t &&error)> done_cb) -> vio::detached_task_t
  {
    co_await self->do_write_index(std::move(bm), fb, ac, tr, cs, std::move(done_cb));
  }(this, std::move(new_blob_manager), free_blobs, attribute_configs, tree_registry, compression_stats, std::move(done));
}

void storage_handler_t::set_compressor(compression_method_t method)
{
  _compressor = create_compressor(method);
}

std::shared_ptr<read_request_t> storage_handler_t::read(storage_location_t location)
{
  auto ret = std::make_shared<read_request_t>();
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
  auto &file = **_file;
  auto buffer = std::make_shared<uint8_t[]>(location.size);
  auto result = co_await vio::read_file(_event_loop, file, buffer.get(), location.size, int64_t(location.offset));

  if (!result.has_value())
  {
    read_request->error.code = result.error().code;
    read_request->error.msg = result.error().msg;
    fprintf(stderr, "Read error %d - %s\n", read_request->error.code, read_request->error.msg.c_str());
  }
  else
  {
    read_request->buffer = buffer;
    read_request->buffer_info.size = uint32_t(result.value());
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

  std::unique_lock<std::mutex> lock(read_request->_mutex);
  read_request->_done = true;
  read_request->_block_for_read.notify_all();
}

} // namespace points::converter
