/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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
#ifndef POINTS_RENDERER_CALLBACKS_P_H
#define POINTS_RENDERER_CALLBACKS_P_H

#include <mutex>

#include <points/render/renderer.h>

namespace points
{
namespace render
{
class callback_manager_t
{
public:
  callback_manager_t(struct renderer_t *renderer)
    : renderer(renderer)
    , user_ptr(nullptr)
  {
    memset(&callbacks, 0, sizeof(callbacks));
  }

  void set_callbacks(const renderer_callbacks_t &cbs, void *u_ptr)
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

  void do_create_buffer(struct buffer_t *buffer)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.create_buffer)
      callbacks.create_buffer(renderer, user_ptr, buffer);
  }
  void do_initialize_buffer(struct buffer_t *buffer)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.initialize_buffer)
      callbacks.initialize_buffer(renderer, user_ptr, buffer);
  }
  void do_modify_buffer(struct buffer_t *buffer)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.modify_buffer)
      callbacks.modify_buffer(renderer, user_ptr, buffer);
  }
  void do_destroy_buffer(struct buffer_t *buffer)
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (callbacks.destroy_buffer)
      callbacks.destroy_buffer(renderer, user_ptr, buffer);
  }
private:
  //TODO: this mutex should be a shared mutex
  std::mutex mutex;
  renderer_callbacks_t callbacks;
  struct renderer_t *renderer;
  void *user_ptr;
};
} // namespace render
} // namespace points

#endif
