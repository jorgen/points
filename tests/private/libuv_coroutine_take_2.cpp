#include "morton.hpp"
#include "threaded_event_loop.hpp"
#include "uv_shared_pointer.h"

#include <catch2/catch.hpp>
#include <uv.h>

#include <coroutine>
#include <iostream>
#include <memory>

#define DELAY 1000

namespace vio
{
template <typename T>
class task_t
{
public:
  task_t(task_t &&t) noexcept
    : _coro(std::exchange(t._coro, {}))
  {
  }

  ~task_t()
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
  }

  struct promise_t
  {
    task_t get_return_object()
    {
      return task_t{std::coroutine_handle<promise_t>::from_promise(*this)};
    }

    void unhandled_exception()
    {
      std::terminate();
    }

    void return_value(T &&value)
    {
      return_value_holder = std::move(value);
    }

    std::suspend_never initial_suspend()
    {
      return {};
    }

    struct final_awaitable_t
    {
      bool await_ready() const noexcept
      {
        return false;
      }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_t> co) noexcept
      {
        if (co.promise().continuation)
        {
          return co.promise().continuation;
        }

        return std::noop_coroutine();
      }

      void await_resume() const noexcept
      {
      }
    };

    final_awaitable_t final_suspend() noexcept
    {
      return {};
    }

    std::coroutine_handle<> continuation;
    T return_value_holder;
  };

  class awaiter;

  awaiter operator co_await() && noexcept;

  using promise_type = promise_t;

private:
  explicit task_t(std::coroutine_handle<promise_t> coro) noexcept
    : _coro(coro)
  {
  }

  std::coroutine_handle<promise_t> _coro;
}; // namespace class task_t

template <typename T>
class task_t<T>::awaiter
{
public:
  bool await_ready() noexcept
  {
    return coro_.done();
  }

  void await_suspend(std::coroutine_handle<> continuation) noexcept
  {
    coro_.promise().continuation = continuation;
  }

  T await_resume() noexcept
  {
    return std::move(coro_.promise().return_value_holder);
  }

  explicit awaiter(std::coroutine_handle<task_t::promise_type> h) noexcept
    : coro_(h)
  {
  }

private:
  std::coroutine_handle<task_t::promise_type> coro_;
};

template <typename T>
task_t<T>::awaiter task_t<T>::operator co_await() && noexcept
{
  fprintf(stderr, "%s\n", __FUNCTION__);
  return awaiter{_coro};
}

template <>
class task_t<void>
{
public:
  task_t(task_t &&t) noexcept
    : _coro(std::exchange(t._coro, {}))
  {
  }

  ~task_t()
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
  }

  struct promise_t
  {
    task_t get_return_object()
    {
      return task_t{std::coroutine_handle<promise_t>::from_promise(*this)};
    }

    void unhandled_exception()
    {
      std::terminate();
    }

    void return_void()
    {
    }

    std::suspend_never initial_suspend()
    {
      return {};
    }

    struct final_awaitable_t
    {
      bool await_ready() const noexcept
      {
        return false;
      }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_t> co) noexcept
      {
        if (co.promise().continuation)
        {
          return co.promise().continuation;
        }

        return std::noop_coroutine();
      }

      void await_resume() const noexcept
      {
      }
    };

    final_awaitable_t final_suspend() noexcept
    {
      return {};
    }

    std::coroutine_handle<> continuation;
  };

  class awaiter;

  awaiter operator co_await() && noexcept;

  using promise_type = promise_t;

private:
  explicit task_t(std::coroutine_handle<promise_t> coro) noexcept
    : _coro(coro)
  {
  }

  std::coroutine_handle<promise_t> _coro;
}; // namespace class task_t

class task_t<void>::awaiter
{
public:
  bool await_ready() noexcept
  {
    return coro_.done();
  }

  void await_suspend(std::coroutine_handle<> continuation) noexcept
  {
    coro_.promise().continuation = continuation;
  }

  void await_resume() noexcept
  {
  }

  explicit awaiter(std::coroutine_handle<task_t::promise_type> h) noexcept
    : coro_(h)
  {
  }

private:
  std::coroutine_handle<task_t::promise_type> coro_;
};

task_t<void>::awaiter task_t<void>::operator co_await() && noexcept
{
  fprintf(stderr, "%s\n", __FUNCTION__);
  return awaiter{_coro};
}

struct uv_co_timer_state
{
  uv_timer_t timer = {};
  bool done = false;
  std::coroutine_handle<> continuation;
};

struct uv_awaitable_timer
{
  ref_ptr_t<uv_co_timer_state> state = make_ref_ptr<uv_co_timer_state>();

  bool await_ready() noexcept
  {
    return state->done;
  }

  void await_suspend(std::coroutine_handle<> continuation) noexcept
  {
    if (state->done)
    {
      continuation.resume();
    }
    else
    {
      state->continuation = continuation;
    }
  }

  void await_resume() noexcept
  {
  }
};

uv_awaitable_timer sleep(points::converter::event_loop_t &event_loop, int milliseconds)
{
  uv_awaitable_timer ret;
  uv_timer_init(event_loop.loop(), &(ret.state->timer));
  auto copy = ret.state;
  ret.state->timer.data = copy.release_to_raw();
  auto callback = [](uv_timer_t *timer)
  {
    uv_timer_stop(timer);
    auto timer_state = ref_ptr_t<uv_awaitable_timer>::from_raw(timer->data);
    timer_state->state->done = true;
    auto to_callback = timer_state;
    timer->data = to_callback.release_to_raw();
    // clang-format off
    auto close_callback = [](uv_handle_t *handle)
    {
      auto timer_state = ref_ptr_t<uv_awaitable_timer>::from_raw(handle->data);
    };
    // clang-format on
    uv_close((uv_handle_t *)timer, close_callback);
    if (timer_state->state->continuation)
      timer_state->state->continuation.resume();
  };
  uv_timer_start(&ret.state->timer, callback, milliseconds, 0);
  return ret;
}
} // namespace vio

vio::task_t<int> sleep_task_2(points::converter::event_loop_t &event_loop)
{
  auto to_wait = vio::sleep(event_loop, DELAY);
  co_await to_wait;
  co_return 1;
}

vio::task_t<void> sleep_task_3(points::converter::event_loop_t &event_loop)
{
  auto to_wait = vio::sleep(event_loop, DELAY * 2);
  co_await to_wait;
}

vio::task_t<int> sleep_task(points::converter::event_loop_t &event_loop)
{
  auto to_wait = vio::sleep(event_loop, DELAY);
  co_await to_wait;

  auto to_wait2 = vio::sleep(event_loop, DELAY * 2);
  auto to_wait3 = vio::sleep(event_loop, DELAY);
  co_await to_wait2;
  co_await to_wait3;
  auto to_wait_4 = sleep_task_2(event_loop);
  co_await sleep_task_3(event_loop);
  co_await std::move(to_wait_4);

  event_loop.stop();
  co_return 3;
}

TEST_CASE("libuv coroutine take 2", "[converter]")
{
  points::converter::thread_pool_t thread_pool(1);
  points::converter::event_loop_t event_loop(thread_pool);
  event_loop.run_in_loop([&event_loop] { sleep_task(event_loop); });
  event_loop.run();
}
