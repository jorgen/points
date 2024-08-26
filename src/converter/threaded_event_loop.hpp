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

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <uv.h>

#include "event_pipe.hpp"
#include "thread_pool.hpp"
#include "worker.hpp"

namespace points
{
namespace converter
{

class about_to_block_t
{
public:
  virtual void about_to_block() = 0;

  template <typename Ret, typename Class, typename... Args>
  std::function<void(Args &&...)> bind(Ret (Class::*f)(Args &&...))
  {
    return [this, f](Args &&...args) { return ((*static_cast<Class *>(this)).*f)(std::move(args)...); };
  }
};

class threaded_event_loop_t
{
public:
  threaded_event_loop_t(thread_pool_t &worker_thread_pool)
    : _loop(nullptr)
    , _worker_thread_pool(worker_thread_pool)
    , _add_pipe([this](std::vector<std::function<uv_handle_t *(uv_loop_t *)>> &&events) { add_event_pipe_cb(std::move(events)); })
    , _run_in_loop([](std::vector<std::function<void()>> &&events) {
      for (auto &to_run : events)
        to_run();
    })
  {
    _to_close_handles.reserve(16);
    barrier_t barrier;
    std::unique_lock<std::mutex> lock(barrier.mutex);

    auto run = [&barrier, this] {
      _loop = new uv_loop_t();
      uv_loop_init(_loop);

      uv_async_init(_loop, &_async_stop, &exit_event_loop_cb);
      _async_stop.data = this;
      _to_close_handles.push_back((uv_handle_t *)&_async_stop);
      _to_close_handles.push_back(_add_pipe.initialize_in_loop(_loop));
      _to_close_handles.push_back(_run_in_loop.initialize_in_loop(_loop));

      uv_prepare_init(_loop, &_about_to_block);
      _about_to_block.data = this;
      uv_prepare_start(&_about_to_block, &about_to_block_cb);
      _to_close_handles.push_back((uv_handle_t *)&_about_to_block);

      {
        std::unique_lock<std::mutex> lock(barrier.mutex);
        barrier.wait.notify_one();
      }

      uv_run(_loop, UV_RUN_DEFAULT);

      uv_loop_close(_loop);
      delete _loop;
      _loop = nullptr;
    };

    _thread.reset(new std::thread(run));
    barrier.wait.wait(lock);
  }

  ~threaded_event_loop_t()
  {
    uv_async_send(&_async_stop);
    _thread->join();
  }

  template <typename... ARGS>
  void add_event_pipe(event_pipe_multi_t<ARGS...> &event_pipe)
  {
    std::function<uv_handle_t *(uv_loop_t *)> func = [&event_pipe](uv_loop_t *loop) { return event_pipe.initialize_in_loop(loop); };
    _add_pipe.post_event(func);
  }

  template <typename... ARGS>
  void add_event_pipe(event_pipe_t<ARGS...> &event_pipe)
  {
    std::function<uv_handle_t *(uv_loop_t *)> func = [&event_pipe](uv_loop_t *loop) { return event_pipe.initialize_in_loop(loop); };
    _add_pipe.post_event(func);
  }

  void add_about_to_block_listener(about_to_block_t *listener)
  {
    _run_in_loop.post_event([this, listener] { _about_to_block_listeners.push_back(listener); });
  }
  void remove_about_to_block_listener(about_to_block_t *listener)
  {
    _run_in_loop.post_event([this, listener] { _about_to_block_listeners.erase(std::remove(_about_to_block_listeners.begin(), _about_to_block_listeners.end(), listener), _about_to_block_listeners.end()); });
  }

  void add_worker_done(worker_t *done)
  {
    _run_in_loop.post_event([done] {
      done->mark_done();
      done->after_work(worker_t::completed);
    });
  }

  uv_loop_t *loop() const
  {
    return _loop;
  }

  std::thread::id thread_id() const
  {
    return _thread->get_id();
  }

  thread_pool_t &worker_thread_pool()
  {
    return _worker_thread_pool;
  }

private:
  void add_event_pipe_cb(std::vector<std::function<uv_handle_t *(uv_loop_t *)>> &&events)
  {
    for (auto f : events)
    {
      _to_close_handles.push_back(f(_loop));
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
    for (auto listener : event_loop->_about_to_block_listeners)
    {
      listener->about_to_block();
    }
  }

  static void exit_event_loop_cb(uv_async_t *handle)
  {
    threaded_event_loop_t *event_loop = static_cast<threaded_event_loop_t *>(handle->data);
    for (auto cancel_handle : event_loop->_to_cancel_work_handles)
      uv_cancel((uv_req_t *)cancel_handle);
    for (auto close_handle : event_loop->_to_close_handles)
    {
      uv_close(close_handle, nullptr);
    }

    uv_prepare_stop(&event_loop->_about_to_block);
  }

  uv_loop_t *_loop;
  uv_async_t _async_stop;
  std::unique_ptr<std::thread> _thread;
  std::mutex _mutex;
  std::vector<uv_handle_t *> _to_close_handles;
  std::vector<uv_work_t *> _to_cancel_work_handles;
  thread_pool_t &_worker_thread_pool;
  event_pipe_multi_t<std::function<uv_handle_t *(uv_loop_t *)>> _add_pipe;
  event_pipe_multi_t<std::function<void()>> _run_in_loop;

  uv_prepare_t _about_to_block;
  std::vector<about_to_block_t *> _about_to_block_listeners;

  friend class worker_t;
};

} // namespace converter
} // namespace points
