/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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

#include <cstring>
#include <mutex>

#include <dew/render/renderer.h>
#include "buffer.hpp"

namespace dew::render
{
class callback_manager_t
{
public:
  callback_manager_t(struct dew_renderer_t *a_renderer)
    : renderer(a_renderer)
    , user_ptr(nullptr)
  {
    memset(&callbacks, 0, sizeof(callbacks));
  }

  void set_callbacks(const dew_renderer_callbacks_t &cbs, void *u_ptr)
  {
    std::unique_lock<std::mutex> lock(mutex);
    this->callbacks = cbs;
    this->user_ptr = u_ptr;
  }

  void do_dirty_callback()
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.dirty)
      callbacks.dirty(this->renderer, user_ptr);
  }

  void do_create_buffer(dew_buffer_t &buffer, dew_buffer_type_t buffer_type)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.create_buffer)
      callbacks.create_buffer(renderer, user_ptr, buffer_type, &buffer.user_ptr);
  }
  void do_initialize_buffer(dew_buffer_t &buffer, enum dew_type_t type, enum dew_components_t components, int buffer_size, void *data)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.initialize_buffer)
      callbacks.initialize_buffer(renderer, user_ptr, &buffer, buffer.user_ptr, type, components, buffer_size, data);
  }
  void do_modify_buffer(dew_buffer_t &buffer, int offset, int buffer_size, void *data)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.modify_buffer)
      callbacks.modify_buffer(renderer, user_ptr, &buffer, buffer.user_ptr, offset, buffer_size, data);
  }

  void do_destroy_buffer(dew_buffer_t &buffer)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.destroy_buffer)
      callbacks.destroy_buffer(renderer, user_ptr, buffer.user_ptr);
    buffer.user_ptr = nullptr;
  }

  void do_create_texture(dew_buffer_t &buffer, enum dew_texture_type_t buffer_texture_type)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.create_texture)
      callbacks.create_texture(renderer, user_ptr, buffer_texture_type, &buffer.user_ptr);
  }

  void do_initialize_texture(dew_buffer_t &buffer, enum dew_texture_type_t buffer_texture_type, enum dew_type_t type, enum dew_components_t components, int size[3], void *data)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.initialize_texture)
      callbacks.initialize_texture(renderer, user_ptr, &buffer, buffer.user_ptr, buffer_texture_type, type, components, size, data);
  }
  void do_modify_texture(dew_buffer_t &buffer, enum dew_texture_type_t buffer_texture_type, int offset[3], int size[3], void *data)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.modify_texture)
      callbacks.modify_texture(renderer, user_ptr, &buffer, &buffer.user_ptr, buffer_texture_type, offset, size, data);
  }
  void do_destroy_texture(dew_buffer_t &buffer)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.destroy_texture)
      callbacks.destroy_texture(renderer, user_ptr, buffer.user_ptr);
  }

private:
  std::mutex mutex;
  dew_renderer_callbacks_t callbacks;
  struct dew_renderer_t *renderer;
  void *user_ptr;
};
} // namespace dew::render
