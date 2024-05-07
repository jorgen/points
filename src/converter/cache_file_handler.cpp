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
#include "cache_file_handler.hpp"

#include "event_pipe.hpp"

#include <uv.h>

#include <fmt/printf.h>

#include <assert.h>
#include <fcntl.h>

#include <utility>
#include <algorithm>

namespace points
{
namespace converter
{

cache_file_request_t::cache_file_request_t(cache_file_handler_t &cache_file_handler, std::function<void(const cache_file_request_t &)> done_callback)
  : cache_file_handler(cache_file_handler)
  , done_callback(std::move(done_callback))
{
}

static void request_done_callback(uv_fs_t *req)
{
  std::unique_ptr<std::shared_ptr<cache_file_request_t>> self_ptr(static_cast<std::shared_ptr<cache_file_request_t> *>(req->data));
  auto self = *self_ptr;
  error_t error;
  if (req->result < 0)
  {
    self->error.code = int(req->result);
    self->error.msg = uv_strerror(int(req->result));
  }
  uv_fs_req_cleanup(req);
  self->done_callback(*self);
}

void cache_file_request_t::do_read(const std::weak_ptr<cache_file_request_t> &self, int64_t offset, int32_t size)
{
  auto self_ptr = new std::weak_ptr<cache_file_request_t>(self);
  uv_request.data = self_ptr;
  buffer = std::make_shared<uint8_t[]>(size);
  uv_buffer.base = (char *)buffer.get();
  uv_buffer.len = size;
  uv_fs_read(cache_file_handler._event_loop.loop(), &uv_request, cache_file_handler._file_handle, &uv_buffer, 1, offset, request_done_callback);
}

void cache_file_request_t::do_write(const std::weak_ptr<cache_file_request_t> &self, const std::shared_ptr<uint8_t[]> &data, uint64_t offset, uint32_t size)
{
  auto self_ptr = new std::shared_ptr<cache_file_request_t>(self);
  uv_request.data = self_ptr;
  buffer = data;
  uv_buffer.base = (char *)buffer.get();
  uv_buffer.len = size;
  uv_fs_write(cache_file_handler._event_loop.loop(), &uv_request, cache_file_handler._file_handle, &uv_buffer, 1, int64_t(offset), request_done_callback);
}

cache_file_handler_t::cache_file_handler_t(const tree_global_state_t &state, std::string cache_file, attributes_configs_t &attributes_configs, event_pipe_t<error_t> &cache_file_error)
  : _cache_file_name(std::move(cache_file))
  , _state(state)
  , _attributes_configs(attributes_configs)
  , _file_handle(0)
  , _file_opened(false)
  , _cache_file_error(cache_file_error)
  , _write_event_pipe(_event_loop, event_bind_t::bind(*this, &cache_file_handler_t::handle_write_events))
{
  (void)_state;
  _open_request.data = this;
#ifdef WIN32
  int open_mode = _S_IREAD | _S_IWRITE;
#else
  int open_mode = 0666;
#endif
  uv_fs_open(_event_loop.loop(), &_open_request, _cache_file_name.c_str(), UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_TRUNC, open_mode, [](uv_fs_t *request) {
    cache_file_handler_t &self = *static_cast<cache_file_handler_t *>(request->data);
    self.handle_open_cache_file(request);
  });
  (void)_attributes_configs;
}

void cache_file_handler_t::handle_open_cache_file(uv_fs_t *request)
{
  _file_handle = uv_file(request->result);
  _file_opened = _file_handle > 0;
  if (_file_handle < 0)
  {
    error_t error;
    error.code = (int)_file_handle;
    error.msg = uv_strerror(_file_handle);
    _cache_file_error.post_event(std::move(error));
  }
}

void cache_file_handler_t::write(const storage_header_t &header, attributes_id_t attributes_id, attribute_buffers_t &&buffers, std::function<void(const storage_header_t &storageheader, attributes_id_t attrib_id, std::vector<storage_location_t> locations, const error_t &error)> done)
{
  _write_event_pipe.post_event(std::make_tuple(header, attributes_id, std::move(buffers), done));
}

void cache_file_handler_t::read(input_data_id_t id, int attribute_index)
{
  (void)id;
  (void)attribute_index;
}

static bool serialize_points(const storage_header_t &header, const buffer_t &points, buffer_t &serialize_data, std::shared_ptr<uint8_t[]> &data_owner)
{
  uint8_t magic[] = {'J', 'L', 'P', 0};
  serialize_data.size = sizeof(magic) + sizeof(header) + points.size;
  data_owner = std::make_shared<uint8_t[]>(serialize_data.size);
  serialize_data.data = data_owner.get();
  auto output_bytes = static_cast<uint8_t *>(serialize_data.data);
  memcpy(output_bytes, magic, sizeof(magic));
  memcpy(output_bytes + sizeof(magic), &header, sizeof(header));
  memcpy(output_bytes + sizeof(magic) + sizeof(header), points.data, points.size);
  return true;
}

void cache_file_handler_t::handle_write_events(std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)>> &&event)
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
    auto &cache_item = _cache_map[location];
    cache_item.ref = 1;
    cache_item.buffer = serialize_data;
    cache_item.data = data_owner;

    auto write_requests_prt = write_requests.get();
    auto request = std::make_shared<cache_file_request_t>(*this, [this, write_requests_prt](const cache_file_request_t &request) {
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
    });
    request->do_write(request, data_owner, location.offset, location.size);
  }
}

points_cache_item_t cache_file_handler_t::ref_points(const storage_location_t &location)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = _cache_map.find(location);
  assert(it != _cache_map.end());
  it->second.ref++;
  points_cache_item_t ret;
  ret.data = it->second.buffer;
  return ret;
}
void cache_file_handler_t::deref_points(const storage_location_t &location)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = _cache_map.find(location);
  assert(it != _cache_map.end());
  it->second.ref--;
}

bool cache_file_handler_t::is_available(input_data_id_t id, int attribute_index)
{
  (void)id;
  (void)attribute_index;
  assert(false);
  return false;
}

int cache_file_handler_t::item_count()
{
  return int(_cache_map.size());
}

void cache_file_handler_t::remove_write_requests(write_requests_t *write_requests)
{
    std::unique_lock<std::mutex> lock(_mutex);
    auto it = std::find_if(_write_requests.begin(), _write_requests.end(), [&](const std::unique_ptr<write_requests_t> &a) { return a.get() == write_requests; });
    assert(it != _write_requests.end());
    _write_requests.erase(it);
}

} // namespace converter
} // namespace points
