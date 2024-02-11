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


#include <uv.h>


#include <fmt/printf.h>

#include <assert.h>
#include <fcntl.h>

namespace points
{
namespace converter
{

static request_id_t get_next_request_id()
{
  static std::atomic<uint64_t> next_id(1);
  return request_id_t{next_id++};
}

cache_file_request_t::cache_file_request_t(cache_file_handler_t &cache_file_handler)
  : cache_file_handler(cache_file_handler)
  , id{0}
{
}

static void request_done_callback(uv_fs_t *req)
{
  std::unique_ptr<std::weak_ptr<cache_file_request_t>> self_ptr(static_cast<std::weak_ptr<cache_file_request_t> *>(req->data));
  auto self = self_ptr->lock();
  if (!self)
    return;
  error_t error;
  if (req->result < 0)
  {
    self->error.code = int(req->result);
    self->error.msg = uv_strerror(int(req->result));
  }
  uv_fs_req_cleanup(req);
  self->cache_file_handler.handle_request_done(self->id);
}

void cache_file_request_t::do_read(request_id_t request_id, const std::weak_ptr<cache_file_request_t> &self, int64_t offset, int32_t size)
{
  this->id = request_id;
  auto self_ptr = new std::weak_ptr<cache_file_request_t>(self);
  uv_request.data = self_ptr;
  buffer = std::make_shared<uint8_t[]>(size);
  uv_buffer.base = (char*) buffer.get();
  uv_buffer.len = size;
  uv_fs_read(cache_file_handler._event_loop.loop(), &uv_request, cache_file_handler._file_handle, &uv_buffer, 1, offset, request_done_callback);

}

void cache_file_request_t::do_write(request_id_t request_id, const std::weak_ptr<cache_file_request_t> &self, const std::shared_ptr<uint8_t[]> &data, int64_t offset, int32_t size)
{
  this->id = request_id;
  auto self_ptr = new std::weak_ptr<cache_file_request_t>(self);
  uv_request.data = self_ptr;
  buffer = data;
  uv_buffer.base = (char *)buffer.get();
  uv_buffer.len = size;
  uv_fs_write(cache_file_handler._event_loop.loop(), &uv_request, cache_file_handler._file_handle, &uv_buffer, 1, offset, request_done_callback);
}

cache_file_handler_t::cache_file_handler_t(const tree_global_state_t &state, const std::string &cache_file, attributes_configs_t &attributes_configs, event_pipe_t<error_t> &cache_file_error, event_pipe_t<std::pair<storage_location_t, storage_header_t>> &write_done)
  : _cache_file_name(cache_file)
  , _state(state)
  , _attributes_configs(attributes_configs)
  , _file_handle(0)
  , _file_opened(false)
  , _cache_file_error(cache_file_error)
  , _write_done(write_done)
  , _write_event_pipe(_event_loop, [this](std::tuple<std::vector<request_id_t>, storage_header_t, attribute_buffers_t,std::function<void(request_id_t id, const error_t &error)>> &&event){this->handle_write_events(std::move(event));})
  , _requests()
{
  (void) _state;
  _open_request.data = this;
#ifdef WIN32
 int open_mode =  _S_IREAD | _S_IWRITE;
#else
  int open_mode = 0666;
#endif
  uv_fs_open(_event_loop.loop(), &_open_request, _cache_file_name.c_str(), UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_TRUNC, open_mode, [](uv_fs_t *request)
  { 
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
    _cache_file_error.post_event(error);
  }
}

std::vector<request_id_t> cache_file_handler_t::write(const storage_header_t &header, attribute_buffers_t &&buffers, std::function<void(request_id_t id, const error_t &error)> done)
{
  std::vector<request_id_t> ret;
  ret.reserve(buffers.buffers.size());
  for (int i = 0; i < int(buffers.buffers.size()); i++)
  {
    ret.push_back(get_next_request_id());
  }
  auto to_tuple = ret;
  _write_event_pipe.post_event(std::make_tuple(std::move(to_tuple), header, std::move(buffers), done));
  return ret;
}
  
request_id_t cache_file_handler_t::read(input_data_id_t id, int attribute_index)
{
  (void)id;
  (void)attribute_index;
  auto read_id = get_next_request_id();
  return read_id;
}

void cache_file_handler_t::handle_write_events(std::tuple<std::vector<request_id_t>, storage_header_t, attribute_buffers_t, std::function<void(request_id_t id, const error_t &error)>> &&event)
{
  auto &storage_header = std::get<1>(event);
  auto &attribute_buffers = std::get<2>(event);
  std::unique_lock<std::mutex> lock(_mutex);
  auto request = std::make_shared<cache_file_request_t>(*this);
  auto &cache_item = _cache_map[storage_header.input_id];
  cache_item.ref = 1;
  cache_item.header = std::move(storage_header);
  cache_item.buffers = std::move(attribute_buffers);
  _write_done.post_event(std::pair<storage_location_t, storage_header_t>({}, cache_item.header));
}
void cache_file_handler_t::handle_request_done(request_id_t id)
{
  (void)id;
}

bool cache_file_handler_t::attribute_id_and_count_for_input_id(input_data_id_t input_id, attributes_id_t &attributes_id, point_count_t &count)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = _cache_map.find(input_id);
  if (it == _cache_map.end())
    return false;

  attributes_id = it->second.attribute_id;
  count.data = uint32_t(it->second.header.point_count);
  return true;
}

points_cache_item_t cache_file_handler_t::ref_points(input_data_id_t id, int attribute_index)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = _cache_map.find(id);
  assert(it != _cache_map.end());
  it->second.ref++;
  points_cache_item_t ret;
  ret.header = it->second.header;
  ret.data = it->second.buffers.buffers[attribute_index];
  return ret;
}
void cache_file_handler_t::deref_points(input_data_id_t id)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = _cache_map.find(id);
  assert(it != _cache_map.end());
  it->second.ref--;
}

int cache_file_handler_t::fill_ids(uint32_t **ids, uint32_t **subs, int buffer_size)
{
  int to_process = std::min(buffer_size, int(_cache_map.size()));
  std::vector<input_data_id_t> input_ids(_cache_map.size());
  auto it = _cache_map.begin();
  for (int i = 0; i < int(_cache_map.size()); ++it, i++)
  {
    input_ids[i] = it->first;
  }

  std::sort(input_ids.begin(), input_ids.end(), [](const input_data_id_t &a, const input_data_id_t &b) { return a.sub == b.sub ? a.data < b.data: a.sub > b.sub; });

  for (int i = 0; i < to_process; i++)
  {
    (*ids)[i] = input_ids[i].data;
    (*subs)[i] = input_ids[i].sub;
  }
  return to_process;
}

bool cache_file_handler_t::is_available(input_data_id_t id, int attribute_index)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto input_data = _cache_map.find(id);
  if (input_data == _cache_map.end())
    return false; 
  if (attribute_index >= input_data->second.buffers.buffers.size())
    return false;
  return input_data->second.buffers.buffers[attribute_index].size > 0;
}

int cache_file_handler_t::item_count()
{
  return int(_cache_map.size());
}

}
} // namespace points
