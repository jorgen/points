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

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace points::converter
{

class thread_pool_t
{
public:
  thread_pool_t(int thread_count = std::thread::hardware_concurrency())
    : stop(false)
  {
    workers.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i)
      workers.emplace_back([this] {
        for (;;)
        {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
            if (this->stop && this->tasks.empty())
              return;
            task = std::move(this->tasks.front());
            this->tasks.pop();
          }
          task();
        }
      });
  }
  ~thread_pool_t()
  {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers)
      worker.join();
  }

  template <typename T>
  std::future<typename std::invoke_result_t<T>> enqueue(T &&task)
  {
    using return_type = typename std::invoke_result_t<T>;

    auto package = std::make_shared<std::packaged_task<return_type()>>(std::forward<T>(task));

    std::future<return_type> res = package->get_future();
    {
      std::unique_lock<std::mutex> lock(queue_mutex);

      if (stop)
      {
        abort(); // invalid state. Enqueing after
      }

      tasks.emplace([package]() { (*package)(); });
    }
    condition.notify_one();
    return res;
  }

private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;

  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
};
} // namespace points::converter
