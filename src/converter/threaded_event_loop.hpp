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

#include "event_pipe.hpp"

namespace points
{
namespace converter
{

class about_to_block_t
{
public:
  virtual void about_to_block() = 0;
};

class threaded_event_loop_t
{
public:
  threaded_event_loop_t()
    : loop(nullptr)
    , add_pipe([this] (std::vector<std::function<uv_handle_t *(uv_loop_t*)>> &&events) { add_event_pipe_cb(std::move(events)); })
    , run_in_loop([](std::vector<std::function<void()>> &&events) { for (auto &to_run : events) to_run(); })
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
      to_close_handles.push_back(run_in_loop.initialize_in_loop(loop));
    
      uv_prepare_init(loop, &about_to_block);
      about_to_block.data = this;
      uv_prepare_start(&about_to_block, &about_to_block_cb);
      to_close_handles.push_back((uv_handle_t *)&about_to_block);

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

  ~threaded_event_loop_t()
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

  void add_about_to_block_listener(about_to_block_t *listener)
  {
    run_in_loop.post_event([this, listener] { about_to_block_listeners.push_back(listener); });
  }
  void remove_about_to_block_listener(about_to_block_t *listener)
  {
    run_in_loop.post_event([this, listener] { about_to_block_listeners.erase(std::remove(about_to_block_listeners.begin(), about_to_block_listeners.end(), listener), about_to_block_listeners.end()); });
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

  static void about_to_block_cb(uv_prepare_t *handle)
  {
    threaded_event_loop_t *event_loop = static_cast<threaded_event_loop_t *>(handle->data);
    for (auto listener : event_loop->about_to_block_listeners)
    {
      listener->about_to_block();
    }
  }

  static void exit_event_loop_cb(uv_async_t *handle)
  {
    threaded_event_loop_t *event_loop = static_cast<threaded_event_loop_t *>(handle->data);
    uv_prepare_stop(&event_loop->about_to_block);
    for (auto close_handle: event_loop->to_close_handles)
      uv_close(close_handle, nullptr);
    for (auto cancel_handle : event_loop->to_cancel_work_handles)
      uv_cancel((uv_req_t *)cancel_handle);
  }

  uv_loop_t *loop;
  uv_async_t async_stop;
  std::unique_ptr<std::thread> thread;
  std::mutex mutex;
  std::vector<uv_handle_t *> to_close_handles;
  std::vector<uv_work_t *> to_cancel_work_handles;
  event_pipe_t<std::function<uv_handle_t *(uv_loop_t*)>> add_pipe;
  event_pipe_t<std::function<void()>> run_in_loop;

  uv_prepare_t about_to_block;
  std::vector<about_to_block_t *> about_to_block_listeners;

  friend class worker_t;
};

} // namespace converter
} // namespace points
