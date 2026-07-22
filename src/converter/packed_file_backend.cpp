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
#include "packed_file_backend.hpp"

#include <uv.h>

#include <cassert>
#include <cstring>
#include <fcntl.h>

namespace points::converter
{

static std::unique_ptr<uint8_t[]> read_into_buffer(vio::event_loop_t &event_loop, uv_file file_handle, uv_fs_t &request, const storage_location_t &location, points_error_t &error)
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

packed_file_backend_t::packed_file_backend_t(std::string file_name, vio::event_loop_t &event_loop, points_error_t &error)
  : _file_name(std::move(file_name))
  , _event_loop(event_loop)
{
  auto stat_result = vio::stat_file(_event_loop, _file_name);
  if (!stat_result.has_value())
  {
    auto index = _blob_manager.register_blob({_serialized_index_size});
    assert(index.data == 0);
    (void)index;
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
}

packed_file_backend_t::~packed_file_backend_t()
{
  _file.reset();
}

bool packed_file_backend_t::exists() const
{
  return _file_exists;
}

points_error_t packed_file_backend_t::open_for_write(bool truncate)
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
    points_error_t error;
    error.code = open_result.error().code;
    error.msg = open_result.error().msg;
    return error;
  }
  _file = std::move(open_result.value());

  if (truncate)
  {
    uv_fs_t request = {};
    uv_fs_ftruncate(_event_loop.loop(), &request, (**_file).handle, 0, NULL);
  }

  return {};
}

points_error_t packed_file_backend_t::read_index(index_load_t &out)
{
  points_error_t error;
  uv_fs_t request = {};
  struct close_on_error_t
  {
    std::optional<vio::auto_close_file_t> &file;
    points_error_t &error;
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
  storage_location_t perf_stats;
  error = deserialize_index(index_buffer.get(), _serialized_index_size, free_blobs, attribute_configs, tree_registry, compression_stats, perf_stats);
  if (error.code != 0)
  {
    return error;
  }

  out.free_blobs = read_into_buffer(_event_loop, file.handle, request, free_blobs, error);
  out.free_blobs_size = free_blobs.size;
  if (!out.free_blobs)
  {
    error.code = 1;
    error.msg = "Failed to read free blobs: " + error.msg;
    return error;
  }

  out.attribute_configs = read_into_buffer(_event_loop, file.handle, request, attribute_configs, error);
  out.attribute_configs_size = attribute_configs.size;
  if (!out.attribute_configs)
  {
    error.code = 1;
    error.msg = "Failed to read attribute_configs: " + error.msg;
    return error;
  }

  out.tree_registry = read_into_buffer(_event_loop, file.handle, request, tree_registry, error);
  out.tree_registry_size = tree_registry.size;
  if (!out.tree_registry)
  {
    error.code = 1;
    error.msg = "Failed to read tree_registry: " + error.msg;
    return error;
  }

  _stats_location = compression_stats;
  _perf_stats_location = perf_stats;
  _tree_registry_location = tree_registry;

  if (compression_stats.size > 0)
  {
    out.stats = read_into_buffer(_event_loop, file.handle, request, compression_stats, error);
    out.stats_size = compression_stats.size;
  }

  if (perf_stats.size > 0)
  {
    out.perf = read_into_buffer(_event_loop, file.handle, request, perf_stats, error);
    out.perf_size = perf_stats.size;
  }

  return error;
}

points_error_t packed_file_backend_t::restore_allocator(const std::unique_ptr<uint8_t[]> &data, uint32_t size)
{
  return _blob_manager.deserialize(data, size);
}

void packed_file_backend_t::allocate_blob(uint32_t size, storage_location_t &out)
{
  std::unique_lock<std::mutex> lock(_mutex);
  out.file_id = 0;
  out.size = size;
  free_blob_manager_t::blob_size_t blob_size = {size};
  out.offset = _blob_manager.register_blob(blob_size).data;
}

vio::task_t<points_error_t> packed_file_backend_t::write_allocated(storage_location_t location, std::shared_ptr<uint8_t[]> data)
{
  assert(location.size > 0);
  assert(data != nullptr);
  auto &file = **_file;
  auto result = co_await vio::write_file(_event_loop, file, data.get(), location.size, int64_t(location.offset));
  points_error_t error;
  if (!result.has_value())
  {
    error.code = result.error().code;
    error.msg = result.error().msg;
  }
  co_return error;
}

vio::task_t<points_error_t> packed_file_backend_t::read_blob(storage_location_t location, uint8_t *dst, uint32_t &bytes_read)
{
  auto &file = **_file;
  auto result = co_await vio::read_file(_event_loop, file, dst, location.size, int64_t(location.offset));
  points_error_t error;
  if (!result.has_value())
  {
    error.code = result.error().code;
    error.msg = result.error().msg;
  }
  else
  {
    bytes_read = uint32_t(result.value());
  }
  co_return error;
}

vio::task_t<points_error_t> packed_file_backend_t::write_index(checkpoint_t checkpoint)
{
  auto make_error = [](std::string msg) {
    points_error_t e;
    e.code = -1;
    e.msg = std::move(msg);
    return e;
  };

  auto new_blob_manager = _blob_manager;
  for (auto &location : checkpoint.freed)
  {
    if (!new_blob_manager.unregister_blob({location.offset}, {location.size}))
      co_return make_error("Failed to remove blob");
  }
  if (_attributes_location.offset > 0)
  {
    if (!new_blob_manager.unregister_blob({_attributes_location.offset}, {_attributes_location.size}))
      co_return make_error("Failed to remove attributes config location");
  }
  if (_blobs_location.offset > 0)
  {
    if (!new_blob_manager.unregister_blob({_blobs_location.offset}, {_blobs_location.size}))
      co_return make_error("Failed to remove blobs location");
  }
  if (_stats_location.offset > 0)
  {
    if (!new_blob_manager.unregister_blob({_stats_location.offset}, {_stats_location.size}))
      co_return make_error("Failed to remove stats location");
  }
  if (_perf_stats_location.offset > 0)
  {
    if (!new_blob_manager.unregister_blob({_perf_stats_location.offset}, {_perf_stats_location.size}))
      co_return make_error("Failed to remove perf stats location");
  }
  // Free the previous tree-registry blob too (it was superseded by checkpoint.tree_registry). Without
  // this the old registry blob leaked its bytes on every checkpoint.
  if (_tree_registry_location.offset > 0)
  {
    if (!new_blob_manager.unregister_blob({_tree_registry_location.offset}, {_tree_registry_location.size}))
      co_return make_error("Failed to remove tree registry location");
  }

  storage_location_t attributes_location;
  attributes_location.offset = new_blob_manager.register_blob({checkpoint.attribute_configs_size}).data;
  attributes_location.size = checkpoint.attribute_configs_size;

  storage_location_t stats_location;
  stats_location.offset = new_blob_manager.register_blob({checkpoint.stats_size}).data;
  stats_location.size = checkpoint.stats_size;

  storage_location_t perf_location;
  perf_location.offset = new_blob_manager.register_blob({checkpoint.perf_size}).data;
  perf_location.size = checkpoint.perf_size;

  auto serialized_blob = new_blob_manager.serialize();
  storage_location_t serialized_blob_location = {0, serialized_blob.size, serialized_blob.offset};

  auto &file = **_file;
  auto make_io_error = [](const vio::error_t &e) {
    points_error_t error;
    error.code = e.code;
    error.msg = e.msg;
    return error;
  };

  auto result1 = co_await vio::write_file(_event_loop, file, reinterpret_cast<const uint8_t *>(serialized_blob.data.get()), serialized_blob.size, int64_t(serialized_blob.offset));
  if (!result1.has_value())
    co_return make_io_error(result1.error());

  auto result2 = co_await vio::write_file(_event_loop, file, checkpoint.attribute_configs.get(), attributes_location.size, int64_t(attributes_location.offset));
  if (!result2.has_value())
    co_return make_io_error(result2.error());

  auto result3 = co_await vio::write_file(_event_loop, file, checkpoint.stats.get(), stats_location.size, int64_t(stats_location.offset));
  if (!result3.has_value())
    co_return make_io_error(result3.error());

  auto result4 = co_await vio::write_file(_event_loop, file, checkpoint.perf.get(), perf_location.size, int64_t(perf_location.offset));
  if (!result4.has_value())
    co_return make_io_error(result4.error());

  // Write the index/superblock LAST (the crash-safety commit point).
  auto serialized_index = serialize_index(_serialized_index_size, serialized_blob_location, attributes_location, checkpoint.tree_registry, stats_location, perf_location);
  auto result = co_await vio::write_file(_event_loop, file, serialized_index.get(), _serialized_index_size, 0);
  if (!result.has_value())
    co_return make_io_error(result.error());

  _blob_manager = std::move(new_blob_manager);
  _blobs_location = serialized_blob_location;
  _attributes_location = attributes_location;
  _stats_location = stats_location;
  _perf_stats_location = perf_location;
  _tree_registry_location = checkpoint.tree_registry;

  uv_fs_t req = {};
  uv_fs_fsync(_event_loop.loop(), &req, file.handle, NULL);
  co_return points_error_t{};
}

} // namespace points::converter
