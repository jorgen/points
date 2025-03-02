#include "morton.hpp"
#include "threaded_event_loop.hpp"

#include <uv.h>
#include <catch2/catch.hpp>

#include <iostream>
#include <coroutine>

#define DELAY 100

class uv_coroutine_t
{
public:
  struct awaiter;
  struct promise_type;
  using coro_handle = std::coroutine_handle<promise_type>;

  coro_handle _co;

  uv_coroutine_t(coro_handle co)
    : _co(co)
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
  }

  uv_coroutine_t(const uv_coroutine_t &) = delete;

  uv_coroutine_t(uv_coroutine_t &&) = delete;

  awaiter operator co_await();
};

class uv_coroutine_event_loop_t : public points::converter::event_loop_t
{
public:
  class async_function_t
  {
  public:
    struct inner_class_t
    {
      inner_class_t(std::function<uv_coroutine_t(uv_coroutine_event_loop_t &)> &&cb)
        : cb(std::move(cb))
          , data(nullptr)
          , called(false)
      {
      }

      std::function<uv_coroutine_t(uv_coroutine_event_loop_t &)> cb;
      void *data;
      bool called;
    };

    async_function_t(std::function<uv_coroutine_t(uv_coroutine_event_loop_t &)> &&cb)
      : _inner(std::make_shared<inner_class_t>(std::move(cb)))
    {
    }

    std::shared_ptr<inner_class_t> _inner;
  };

  explicit uv_coroutine_event_loop_t(points::converter::thread_pool_t &worker_thread_pool)
    : points::converter::event_loop_t(worker_thread_pool)
      , _async_callbacks(*this, bind(&uv_coroutine_event_loop_t::call_async_callback))
      , _async_coroutine_callbacks(*this, bind(&uv_coroutine_event_loop_t::call_async_coroutine))
  {
  }

  void async_callback(std::function<void()> &&cb)
  {
    _async_callbacks.post_event(std::move(cb));
  }

  async_function_t async_coroutine(std::function<uv_coroutine_t(uv_coroutine_event_loop_t &)> &&cb)
  {
    async_function_t ret(std::move(cb));
    auto inner_copy = ret._inner;
    _async_coroutine_callbacks.post_event(std::move(inner_copy));
    return ret;
  }

private:
  template <typename Ret, typename Class, typename... Args>
  std::function<void(Args &&...)> bind(Ret (Class::*f)(Args &&...))
  {
    return [this, f](Args &&... args) {
      return ((*static_cast<Class *>(this)).*f)(std::move(args)...);
    };
  }

  void call_async_callback(std::function<void()> &&cb)
  {
    cb();
  }

  void call_async_coroutine(std::shared_ptr<async_function_t::inner_class_t> &&inner_class)
  {
    inner_class->cb(*this);
    inner_class->called = true;
    if (inner_class->data)
    {
      auto co = std::coroutine_handle<>::from_address(inner_class->data);
      co.resume();
    }
  }

  points::converter::event_pipe_t<std::function<void()> > _async_callbacks;
  points::converter::event_pipe_t<std::shared_ptr<async_function_t::inner_class_t> > _async_coroutine_callbacks;
};

struct sleep_t
{
  unsigned long _delay;

  explicit sleep_t(unsigned long delay)
    : _delay(delay)
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
  }
};

struct function_t
{
  std::function<void()> func;

  explicit function_t(std::function<void()> &&func)
    : func(std::move(func))
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
  }
};

struct coroutine_function_t
{
  uv_coroutine_event_loop_t &event_loop;
  std::function<uv_coroutine_t(uv_coroutine_event_loop_t &)> func;

  explicit coroutine_function_t(uv_coroutine_event_loop_t &event_loop, std::function<uv_coroutine_t(uv_coroutine_event_loop_t &)> &&func)
    : event_loop(event_loop)
      , func(std::move(func))
  {
  }
};

void on_sleep_done(uv_timer_t *timer)
{
  fprintf(stderr, "%s\n", __FUNCTION__);
  auto co = std::coroutine_handle<>::from_address(timer->data);
  delete timer;
  co.resume();
}

struct final_awaitable
{
  std::coroutine_handle<> _co;

  explicit final_awaitable(std::coroutine_handle<> co)
    : _co(co)
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
  }

  bool await_ready() noexcept
  {
    return false;
  }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
    if (_co)
    {
      return _co;
    }
    else
    {
      return std::noop_coroutine();
    }
  }

  void await_resume() noexcept
  {
  }
};

class uv_coroutine_event_loop_t;

struct uv_coroutine_t::promise_type
{
  using coro_handle = std::coroutine_handle<promise_type>;

  uv_coroutine_event_loop_t &_event_loop;
  std::function<uv_coroutine_t()> _func;
  std::coroutine_handle<> _continuation;

  promise_type(uv_coroutine_event_loop_t &event_loop)
    : _event_loop(event_loop)
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
  }

  promise_type(uv_coroutine_event_loop_t &event_loop, std::function<uv_coroutine_t()> &&func)
    : _event_loop(event_loop)
      , _func(std::move(func))
  {
  }


  auto get_return_object()
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
    return coro_handle::from_promise(*this);
  }

  std::suspend_never initial_suspend()
  {
    return {};
  }

  auto final_suspend() noexcept
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
    return final_awaitable{_continuation};
  }

  void return_void()
  {
  }

  template <typename T>
  auto &&await_transform(T &&t) const noexcept
  {
    fprintf(stderr, "%s %d\n", __FUNCSIG__, __LINE__);
    return std::move(t);
  }

  auto await_transform(sleep_t sleep_cmd)
  {
    fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
    uv_timer_t *timer = new uv_timer_t{};
    uv_timer_init(_event_loop.loop(), timer);
    timer->data = coro_handle::from_promise(*this).address();
    uv_timer_start(timer, &on_sleep_done, sleep_cmd._delay, 0);
    return std::suspend_always{};
  }


  auto await_transform(uv_coroutine_event_loop_t::async_function_t func)
  {
    struct async_function_awaitable_t
    {
      bool is_done = false;

      async_function_awaitable_t(bool is_done)
        : is_done(is_done)
      {
      }

      constexpr bool await_ready() const noexcept
      {
        return is_done;
      }

      constexpr void await_suspend(std::coroutine_handle<>) const noexcept
      {
      }

      constexpr void await_resume() const noexcept
      {
      }
    };
    fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
    assert(func._inner);
    assert(func._inner->data == nullptr);
    func._inner->data = coro_handle::from_promise(*this).address();
    return async_function_awaitable_t(func._inner->called);
  }

  struct internal_uv_async_t : uv_async_t
  {
    std::function<void()> func;
  };

  auto await_transform(function_t &&func)
  {
    fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
    internal_uv_async_t *async = new internal_uv_async_t{};
    async->func = std::move(func.func);
    async->data = coro_handle::from_promise(*this).address();
    uv_async_init(_event_loop.loop(), async, [](uv_async_t *async) {
      internal_uv_async_t *async2 = (internal_uv_async_t *)async;
      async2->func();
      uv_close((uv_handle_t *)async, [](uv_handle_t *handle) {
        delete (internal_uv_async_t *)handle;
      });
      auto co = std::coroutine_handle<>::from_address(async->data);
      co.resume();
    });
    uv_async_send(async);
    return std::suspend_always{};
  }

  auto &&await_transform(std::function<uv_coroutine_t()> &&func) const noexcept
  {
    return std::move(func);
  }

  void unhandled_exception()
  {
    std::terminate();
  }
};

struct uv_coroutine_t::awaiter
{
  using coro_handle = std::coroutine_handle<uv_coroutine_t::promise_type>;

  coro_handle _co;

  awaiter(coro_handle co)
    : _co(co)
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
  };

  bool await_ready()
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
    return false;
  }

  auto await_suspend(std::coroutine_handle<> co_cont)
  {
    fprintf(stderr, "%s\n", __FUNCTION__);
    _co.promise()._continuation = co_cont;
    return true;
  }

  void await_resume()
  {
  }
};

uv_coroutine_t::awaiter uv_coroutine_t::operator co_await()
{
  return {_co};
}

uv_coroutine_t third(uv_coroutine_event_loop_t &event_loop)
{
  fprintf(stderr, "4\n");
  co_await sleep_t(DELAY);
  fprintf(stderr, "5\n");
  co_await sleep_t(DELAY);
  fprintf(stderr, "6\n");
  co_await sleep_t(DELAY);
}

uv_coroutine_t second(uv_coroutine_event_loop_t &event_loop)
{
  fprintf(stderr, "2\n");
  co_await sleep_t(DELAY);
  fprintf(stderr, "3\n");
  co_await sleep_t(DELAY);
  for (int i = 0; i < 3; i++)
  {
    co_await third(event_loop);
  }
  fprintf(stderr, "7\n");
  co_await sleep_t(DELAY);
}

uv_coroutine_t fifth(uv_coroutine_event_loop_t &event_loop)
{
  (void)event_loop;
  fprintf(stderr, "13\n");
  co_await function_t([]() {
    fprintf(stderr, "14\n");
  });
}

uv_coroutine_t fourth(uv_coroutine_event_loop_t &event_loop)
{
  fprintf(stderr, "10\n");
  co_await function_t([]() {
    fprintf(stderr, "11\n");
  });
  fprintf(stderr, "12\n");
  co_await event_loop.async_coroutine(fifth);
  event_loop.stop();
}

uv_coroutine_t sixth(uv_coroutine_event_loop_t &event_loop)
{
  (void)event_loop;
  fprintf(stderr, "14\n");
  co_await sleep_t(DELAY / 2);
  fprintf(stderr, "16\n");
}

uv_coroutine_t seventh(uv_coroutine_event_loop_t &event_loop)
{
  (void)event_loop;
  fprintf(stderr, "15\n");
  co_await sleep_t(DELAY);
  fprintf(stderr, "17\n");
}


uv_coroutine_t service_main(uv_coroutine_event_loop_t &event_loop)
{
  fprintf(stderr, "1\n");
  co_await sleep_t(DELAY);
  co_await second(event_loop);
  fprintf(stderr, "8\n");
  co_await sleep_t(DELAY);
  fprintf(stderr, "9\n");
  co_await fourth(event_loop);

  auto sixth_coro = sixth(event_loop);
  co_await seventh(event_loop);
  co_await sixth_coro;
}

TEST_CASE("libuv coroutine", "[converter]")
{
  points::converter::thread_pool_t thread_pool(1);
  uv_coroutine_event_loop_t event_loop(thread_pool);
  fprintf(stderr, "%s - %d\n", __FUNCTION__, __LINE__);
  service_main(event_loop);
  fprintf(stderr, "%s - %d\n", __FUNCTION__, __LINE__);
  event_loop.run();
}
