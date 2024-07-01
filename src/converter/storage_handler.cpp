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

#include "event_pipe.hpp"

#include <uv.h>

#include <fmt/printf.h>

#include <assert.h>
#include <fcntl.h>

#include <algorithm>
#include <utility>

#include <fmt/format.h>

namespace points::converter
{

storage_handler_request_t::storage_handler_request_t(storage_handler_t &storage_handler, std::function<void(const storage_handler_request_t &)> done_callback)
  : storage_handler(storage_handler)
  , done_callback(std::move(done_callback))
{
}

static void request_done_callback(uv_fs_t *req)
{
  auto self = static_cast<storage_handler_request_t *>(req->data);
  error_t error;
  if (req->result < 0)
  {
    self->error.code = int(req->result);
    self->error.msg = uv_strerror(int(req->result));
    fprintf(stderr, "Write error %d - %s\n", self->error.code, self->error.msg.c_str());
  }
  else
  {
    self->buffer_info.size = uint32_t(req->result);
    self->buffer_info.data = self->buffer.get();
  }
  uv_fs_req_cleanup(req);
  self->done_callback(*self);
  self->storage_handler.remove_request(self);
}

void storage_handler_request_t::do_read(uint64_t offset, uint32_t size)
{
  assert(std::this_thread::get_id() == storage_handler._event_loop.thread_id());
  uv_request.data = this;
  buffer = std::make_shared<uint8_t[]>(size);
  uv_buffer.base = (char *)buffer.get();
  uv_buffer.len = size;
  uv_fs_read(storage_handler._event_loop.loop(), &uv_request, storage_handler._file_handle, &uv_buffer, 1, int64_t(offset), request_done_callback);
}

void storage_handler_request_t::do_write(const std::shared_ptr<uint8_t[]> &data, uint64_t offset, uint32_t size)
{
  assert(std::this_thread::get_id() == storage_handler._event_loop.thread_id());
  uv_request.data = this;
  buffer = data;
  uv_buffer.base = (char *)buffer.get();
  uv_buffer.len = size;
  assert(size > 0);
  assert(data != nullptr);
  uv_fs_write(storage_handler._event_loop.loop(), &uv_request, storage_handler._file_handle, &uv_buffer, 1, int64_t(offset), request_done_callback);
}

read_request_t::read_request_t(storage_handler_t &storage, std::function<void(const storage_handler_request_t &)> done)
  : storage_handler_request_t(storage,
                              [this, done = std::move(done)](const storage_handler_request_t &request) {
                                std::unique_lock<std::mutex> lock(this->_mutex);
                                this->_done = true;
                                this->_block_for_read.notify_all();
                                done_callback(request);
                              })
  , _done(false)
{
}

void read_request_t::wait_for_read()
{
  std::unique_lock<std::mutex> lock(_mutex);
  _block_for_read.wait(lock, [this] { return this->_done; });
}

storage_handler_t::storage_handler_t(const std::string &url, attributes_configs_t &attributes_configs, event_pipe_t<error_t> &storage_error_pipe)
  : _cache_file_name(url)
  , _file_handle(0)
  , _file_opened(false)
  , _attributes_configs(attributes_configs)
  , _serialized_index_size(128)
  , _storage_error(storage_error_pipe)
  , _write_event_pipe(_event_loop, event_bind_t::bind(*this, &storage_handler_t::handle_write_events))
  , _write_trees_pipe(_event_loop, event_bind_t::bind(*this, &storage_handler_t::handle_write_trees))
  , _write_tree_registry_pipe(_event_loop, event_bind_t::bind(*this, &storage_handler_t::handle_write_tree_registry))
  , _write_blob_locations_and_update_header_pipe(_event_loop, event_bind_t::bind(*this, &storage_handler_t::handle_write_blob_locations_and_update_header))
  , _read_request_pipe(_event_loop, event_bind_t::bind(*this, &storage_handler_t::handle_read_request))
{
  auto index = _blob_manager.register_blob({_serialized_index_size});
  assert(index.data == 0);
  _open_request.data = this;
#ifdef WIN32
  int open_mode = _S_IREAD | _S_IWRITE;
#else
  int open_mode = 0666;
#endif
  uv_fs_open(_event_loop.loop(), &_open_request, _cache_file_name.c_str(), UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_TRUNC, open_mode, [](uv_fs_t *request) {
    storage_handler_t &self = *static_cast<storage_handler_t *>(request->data);
    self.handle_open_cache_file(request);
  });
}

void storage_handler_t::handle_open_cache_file(uv_fs_t *request)
{
  _file_handle = uv_file(request->result);
  if (_file_handle < 0)
  {
    error_t error;
    error.code = (int)_file_handle;
    error.msg = uv_strerror(_file_handle);
    _storage_error.post_event(std::move(error));
  }
  _file_opened = true;
  _block_for_open.notify_all();
}
error_t storage_handler_t::wait_for_open()
{
  std::unique_lock<std::mutex> lock(_mutex);
  _block_for_open.wait(lock, [this] { return this->_file_opened.load(); });
  return _open_error;
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

void storage_handler_t::handle_write_events(
  std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)>> &&event)
{
  auto &&[storage_header, attributes_id, attribute_buffers, done] = std::move(event);
  std::unique_lock<std::mutex> lock(_mutex);
  auto &write_requests = this->_write_requests.emplace_back(new write_requests_t());
  write_requests->target_count = int(attribute_buffers.buffers.size());
  std::vector<storage_location_t> &locations = write_requests->locations;
  locations.resize(attribute_buffers.buffers.size());
  write_requests->header = storage_header;
  write_requests->attributes_id = attributes_id;
  write_requests->done_callback = std::move(done);
  for (int i = 0; i < int(attribute_buffers.buffers.size()); i++)
  {
    buffer_t serialize_data;
    std::shared_ptr<uint8_t[]> data_owner;
    if (i == 0)
    {
      serialize_points(storage_header, attribute_buffers.buffers[i], serialize_data, data_owner);
    }
    else
    {
      serialize_data = attribute_buffers.buffers[i];
      data_owner = std::move(attribute_buffers.data[i]);
    }
    auto &location = locations[i];
    location.file_id = 0;
    location.size = serialize_data.size;
    free_blob_manager_t::blob_size_t size = {location.size};
    location.offset = this->_blob_manager.register_blob(size).data;

    auto write_requests_prt = write_requests.get();
    auto done_callback = [this, write_requests_prt](const storage_handler_request_t &request) {
      if (request.error.code != 0 && write_requests_prt->error.code == 0)
      {
        write_requests_prt->error = request.error;
      }
      write_requests_prt->done++;
      if (write_requests_prt->done == write_requests_prt->target_count)
      {
        if (write_requests_prt->done_callback)
        {
          write_requests_prt->done_callback(write_requests_prt->header, write_requests_prt->attributes_id, std::move(write_requests_prt->locations), write_requests_prt->error);
        }
        remove_write_requests(write_requests_prt);
      }
    };
    handle_write(data_owner, location, std::move(done_callback));
  }
}

void storage_handler_t::handle_write_trees(std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&)>> &&event)
{
  auto &&[tree_ids, serialized_trees, done] = std::move(event);
  std::unique_lock<std::mutex> lock(_mutex);
  auto &write_requests = this->_write_trees_requests.emplace_back(new write_trees_request_t());
  write_requests->target_count = int(tree_ids.size());
  auto &locations = write_requests->locations;
  locations.resize(tree_ids.size());
  write_requests->done_callback = std::move(done);
  write_requests->serialized_trees = std::move(serialized_trees);
  write_requests->tree_ids = std::move(tree_ids);
  for (int i = 0; i < write_requests->target_count; i++)
  {
    auto &location = locations[i];
    location.file_id = 0;
    location.size = write_requests->serialized_trees[i].size;
    free_blob_manager_t::blob_size_t size = {location.size};
    location.offset = this->_blob_manager.register_blob(size).data;
    auto write_requests_prt = write_requests.get();
    auto done_callback = [this, write_requests_prt](const storage_handler_request_t &request) {
      if (request.error.code != 0 && write_requests_prt->error.code == 0)
      {
        write_requests_prt->error = request.error;
      }
      write_requests_prt->done++;
      if (write_requests_prt->done == write_requests_prt->target_count)
      {
        if (write_requests_prt->done_callback)
        {
          write_requests_prt->done_callback(std::move(write_requests_prt->tree_ids), std::move(write_requests_prt->locations), std::move(write_requests_prt->error));
        }
        remove_write_tree_requests(write_requests_prt);
      }
    };
    handle_write(write_requests->serialized_trees[i].data, location, std::move(done_callback));
  }
}

void storage_handler_t::handle_write_tree_registry(serialized_tree_registry_t &&serialized_tree, std::function<void(storage_location_t, error_t &&error)> &&done)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &write_requests = this->_write_tree_registry_requests.emplace_back(new write_tree_registry_request_t());
  write_requests->done_callback = std::move(done);
  write_requests->serialized_tree_registry = std::move(serialized_tree);
  auto &location = write_requests->location;
  location.file_id = 0;
  location.size = write_requests->serialized_tree_registry.size;
  free_blob_manager_t::blob_size_t size = {location.size};
  location.offset = this->_blob_manager.register_blob(size).data;
  auto write_requests_prt = write_requests.get();
  auto done_callback = [this, write_requests_prt](const storage_handler_request_t &request) {
    if (request.error.code != 0 && write_requests_prt->error.code == 0)
    {
      write_requests_prt->error = request.error;
    }
    if (write_requests_prt->done_callback)
    {
      write_requests_prt->done_callback(write_requests_prt->location, std::move(write_requests_prt->error));
    }
    remove_write_tree_registry_requests(write_requests_prt);
  };
  handle_write(write_requests->serialized_tree_registry.data, location, std::move(done_callback));
}

struct serialize_meta_t
{
  free_blob_manager_t new_blob_manager;
  serialized_free_blob_manager_t serialized_blob;
  serialized_attributes_t serialized_attributes_configs;
  storage_location_t serialized_attributes_configs_location;
  int serialized_count = 0;
  std::function<void(error_t &&error)> done;
};

void storage_handler_t::handle_write_blob_locations_and_update_header(storage_location_t &&new_tree_registry_location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> &&done)
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
      return;
    }
  }
  if (attributes_location.offset > 0)
  {
    auto removed = new_blob_manager.unregister_blob({attributes_location.offset}, {attributes_location.size});
    if (!removed)
    {
      error_t error;
      error.code = -1;
      error.msg = "Failed to remove attributes config location";
      done(std::move(error));
      return;
    }
  }

  if (blobs_location.offset > 0)
  {
    auto removed = new_blob_manager.unregister_blob({blobs_location.offset}, {blobs_location.size});
    if (!removed)
    {
      error_t error;
      error.code = -1;
      error.msg = "Failed to remove blobs location";
      done(std::move(error));
      return;
    }
  }

  auto serialized_meta = std::make_shared<serialize_meta_t>();
  serialized_meta->serialized_attributes_configs = _attributes_configs.serialize();
  serialized_meta->serialized_attributes_configs_location.offset = new_blob_manager.register_blob({serialized_meta->serialized_attributes_configs.size}).data;
  serialized_meta->serialized_attributes_configs_location.size = serialized_meta->serialized_attributes_configs.size;
  serialized_meta->serialized_blob = new_blob_manager.serialize();

  serialized_meta->new_blob_manager = std::move(new_blob_manager);
  serialized_meta->done = std::move(done);

  auto request_handler = [this, new_tree_registry_location, serialized_meta](const storage_handler_request_t &request) mutable {
    error_t error;
    serialized_meta->serialized_count++;
    if (request.error.code != 0)
    {
      error = request.error;
      serialized_meta->done(std::move(error));
      return;
    }

    if (serialized_meta->serialized_count == 2)
    {
      storage_location_t serialized_blob_location = {0, serialized_meta->serialized_blob.size, serialized_meta->serialized_blob.offset};
      handle_write_index(std::move(serialized_meta->new_blob_manager), serialized_blob_location, serialized_meta->serialized_attributes_configs_location, new_tree_registry_location, std::move(serialized_meta->done));
    }
  };

  handle_write(serialized_meta->serialized_blob.data, {0, serialized_meta->serialized_blob.size, serialized_meta->serialized_blob.offset}, request_handler);

  handle_write(serialized_meta->serialized_attributes_configs.data, {0, serialized_meta->serialized_attributes_configs_location.size, serialized_meta->serialized_attributes_configs_location.offset}, request_handler);
}

void storage_handler_t::handle_write_index(free_blob_manager_t &&new_blob_manager, const storage_location_t &free_blobs, const storage_location_t &attribute_configs, const storage_location_t &tree_registry,
                                           std::function<void(error_t &&error)> &&done)
{
  auto serialized_index = std::make_shared<uint8_t[]>(_serialized_index_size);
  auto *data = serialized_index.get();
  memset(data, 0, _serialized_index_size);

  uint8_t magic[] = {'J', 'L', 'P', 0};
  memcpy(data, magic, sizeof(magic));
  data += sizeof(magic);

  memcpy(data, &free_blobs, sizeof(free_blobs));
  data += sizeof(free_blobs);

  memcpy(data, &tree_registry, sizeof(tree_registry));
  data += sizeof(tree_registry);

  memcpy(data, &attribute_configs, sizeof(attribute_configs));
  data += sizeof(attribute_configs);

  auto done_callback = [this, new_blob_manager = std::move(new_blob_manager), free_blobs, attribute_configs, done = std::move(done)](const storage_handler_request_t &request) mutable {
    error_t error;
    fmt::print(stderr, "Write index done {}\n", request.error.code);
    if (request.error.code != 0)
    {
      error = request.error;
      done(std::move(error));
    }
    this->_blob_manager = std::move(new_blob_manager);
    this->blobs_location = free_blobs;
    this->attributes_location = attribute_configs;
    done(std::move(error));
  };

  fprintf(stderr, "Writing index\n");
  handle_write(serialized_index, {0, _serialized_index_size, 0}, std::move(done_callback));
}

void storage_handler_t::remove_write_requests(write_requests_t *write_requests)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = std::find_if(_write_requests.begin(), _write_requests.end(), [&](const std::unique_ptr<write_requests_t> &a) { return a.get() == write_requests; });
  assert(it != _write_requests.end());
  _write_requests.erase(it);
}

void storage_handler_t::remove_write_tree_requests(write_trees_request_t *write_requests)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = std::find_if(_write_trees_requests.begin(), _write_trees_requests.end(), [&](const std::unique_ptr<write_trees_request_t> &a) { return a.get() == write_requests; });
  assert(it != _write_trees_requests.end());
  _write_trees_requests.erase(it);
}

void storage_handler_t::remove_write_tree_registry_requests(write_tree_registry_request_t *write_requests)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = std::find_if(_write_tree_registry_requests.begin(), _write_tree_registry_requests.end(), [&](const std::unique_ptr<write_tree_registry_request_t> &a) { return a.get() == write_requests; });
  assert(it != _write_tree_registry_requests.end());
  _write_tree_registry_requests.erase(it);
}
std::shared_ptr<read_request_t> storage_handler_t::read(storage_location_t location, std::function<void(const storage_handler_request_t &)> done_callback)
{
  auto ret = std::make_shared<read_request_t>(*this, std::move(done_callback));
  auto copy = ret;
  _read_request_pipe.post_event(std::move(copy), std::move(location));
  return ret;
}

void storage_handler_t::add_request(std::shared_ptr<storage_handler_request_t> request)
{
  std::unique_lock<std::mutex> lock(_requests_mutex);
  _requests.emplace_back(std::move(request));
}

void storage_handler_t::remove_request(storage_handler_request_t *request)
{
  std::unique_lock<std::mutex> lock(_requests_mutex);
  auto it = std::find_if(_requests.begin(), _requests.end(), [&](const std::shared_ptr<storage_handler_request_t> &a) { return a.get() == request; });
  assert(it != _requests.end());
  _requests.erase(it);
}
void storage_handler_t::handle_read_request(std::shared_ptr<read_request_t> &&read_request, storage_location_t &&location)
{
  add_request(read_request);
  read_request->do_read(location.offset, location.size);
}
std::shared_ptr<storage_handler_request_t> storage_handler_t::handle_write(const std::shared_ptr<uint8_t[]> &data, const storage_location_t &location, std::function<void(const storage_handler_request_t &)> done_callback)
{
  auto ret = std::make_shared<storage_handler_request_t>(*this, std::move(done_callback));
  add_request(ret);
  ret->do_write(data, location.offset, location.size);
  return ret;
}

} // namespace points::converter
