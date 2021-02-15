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
#include <memory>
#include <thread>
#include <functional>
#include <mutex>
#include <utility>

namespace points
{
namespace converter
{
class threaded_event_loop_t
{
public:
  threaded_event_loop_t()
    : loop(nullptr)
    , add_pipe([this] (std::vector<std::function<uv_handle_t *(uv_loop_t*)>> &&events) { add_event_pipe_cb(std::move(events)); })
  {
    to_close_handles.reserve(16);
    barrier_t barrier;
    std::unique_lock<std::mutex> lock(barrier.mutex);

    auto run = [&barrier, this] {
      loop = new uv_loop_t();
      uv_loop_init(loop);

      uv_async_init(loop, &async_stop, &exit_event_loop_cb);
      async_stop.data = this;
      to_close_handles.push_back((uv_handle_t *)&async_stop);
      to_close_handles.push_back(add_pipe.initialize_in_loop(loop));
      

      {
        std::unique_lock<std::mutex> lock(barrier.mutex);
        barrier.wait.notify_one();
      }

      uv_run(loop, UV_RUN_DEFAULT);

      uv_loop_close(loop);
      delete loop;
      loop = nullptr;
    };

    thread.reset(new std::thread(run));
    barrier.wait.wait(lock);
  }

  threaded_event_loop_t::~threaded_event_loop_t()
  {
    uv_async_send(&async_stop);
    thread->join();
  }

  template<typename T>
  void add_event_pipe(event_pipe_t<T> &event_pipe)
  {
    std::function<uv_handle_t *(uv_loop_t *)> func = [&event_pipe](uv_loop_t *loop) { return event_pipe.initialize_in_loop(loop); };
    add_pipe.post_event(func);
  }

private:
  void add_event_pipe_cb(std::vector<std::function<uv_handle_t *(uv_loop_t*)>> &&events)
  {
    for (auto f : events)
    {
      to_close_handles.push_back(f(loop));
    }
  }

  struct barrier_t
  {
    std::mutex mutex;
    std::condition_variable wait;
  };

  static void exit_event_loop_cb(uv_async_t *handle)
  {
    threaded_event_loop_t *event_loop = static_cast<threaded_event_loop_t *>(handle->data);
    for (auto close_handle: event_loop->to_close_handles)
      uv_close(close_handle, nullptr);
  }

  uv_loop_t *loop;
  uv_async_t async_stop;
  std::unique_ptr<std::thread> thread;
  std::mutex mutex;
  std::vector<uv_handle_t *> to_close_handles;
  event_pipe_t<std::function<uv_handle_t *(uv_loop_t*)>> add_pipe;
};

} // namespace converter
} // namespace points
