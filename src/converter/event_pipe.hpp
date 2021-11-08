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
#pragma once

#include <uv.h>
#include <vector>
#include <mutex>
#include <functional>

namespace points
{
namespace converter
{
template <typename T>
class event_pipe_t
{
public:
  template<typename EventLoop>
  event_pipe_t(EventLoop &eventLoop, std::function<void(std::vector<T> &&events)> event_callback)
    : event_callback(event_callback)
  {
    pipe.data = this;
    eventLoop.add_event_pipe(*this);
  }
  
  event_pipe_t(std::function<void(std::vector<T> &&events)> event_callback)
    : event_callback(event_callback)
  {
    pipe.data = this;
  }

  uv_handle_t *initialize_in_loop(uv_loop_t *loop)
  {
    auto on_event = [](uv_async_t *handle) {
      event_pipe_t *event_pipe = static_cast<event_pipe_t*>(handle->data);
      std::vector<T> event_vec;
      event_pipe->swap_events(event_vec);
      event_pipe->event_callback(std::move(event_vec));
    };
    uv_async_init(loop, &pipe, on_event);

    return (uv_handle_t *)&pipe;
  }

  void post_event(const T &t)
  {
    std::unique_lock<std::mutex> lock(mutex);
    events.push_back(t);
    uv_async_send(&pipe);
  }

  void post_event(T &&t)
  {
    std::unique_lock<std::mutex> lock(mutex);
    events.push_back(std::move(t));
    uv_async_send(&pipe);
  }

  void swap_events(std::vector<T> &to_swap)
  {
    std::unique_lock<std::mutex> lock(mutex);
    std::swap(events, to_swap);
    events.reserve(to_swap.capacity());
  }

private:
  std::function<void(std::vector<T> &&events)> event_callback;
  std::vector<T> events;
  uv_async_t pipe;
  std::mutex mutex;
};
}
} // namespace points

