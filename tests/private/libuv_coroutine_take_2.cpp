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

      void await_suspend(std::coroutine_handle<promise_t> co) noexcept
      {
        fprintf(stderr, "%s\n", __FUNCTION__);
        if (co.promise().continuation)
          co.promise().continuation.resume();
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

class task_t::awaiter
{
public:
  bool await_ready() noexcept
  {
    return false;
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

task_t::awaiter task_t::operator co_await() && noexcept
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

vio::task_t sleep_task_2(points::converter::event_loop_t &event_loop)
{
  fprintf(stderr, "%s 1\n", __FUNCTION__);
  auto to_wait = vio::sleep(event_loop, DELAY);
  fprintf(stderr, "%s 2\n", __FUNCTION__);
  co_await to_wait;
  fprintf(stderr, "%s 3\n", __FUNCTION__);
  co_return;
}

vio::task_t sleep_task(points::converter::event_loop_t &event_loop)
{
  auto to_wait = vio::sleep(event_loop, DELAY);
  co_await to_wait;

  auto to_wait2 = vio::sleep(event_loop, DELAY * 2);
  auto to_wait3 = vio::sleep(event_loop, DELAY);
  co_await to_wait2;
  co_await to_wait3;
  fprintf(stderr, "%s 1\n", __FUNCTION__);
  auto to_wait_4 = sleep_task_2(event_loop);
  fprintf(stderr, "%s 2\n", __FUNCTION__);
  co_await std::move(to_wait_4);
  fprintf(stderr, "%s 3\n", __FUNCTION__);

  event_loop.stop();
}

TEST_CASE("libuv coroutine take 2", "[converter]")
{
  points::converter::thread_pool_t thread_pool(1);
  points::converter::event_loop_t event_loop(thread_pool);
  event_loop.run_in_loop(
    [&event_loop]
    {
      fprintf(stderr, "calling sleep_task 1\n");
      sleep_task(event_loop);
      fprintf(stderr, "calling sleep_task 2\n");
    });
  event_loop.run();
}
