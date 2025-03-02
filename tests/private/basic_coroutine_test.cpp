#include <catch2/catch.hpp>
#include <coroutine>
#include <fmt/printf.h>

struct task_t
{
  struct promise_t
  {
    int value;

    task_t get_return_object()
    {
      return task_t{std::coroutine_handle<promise_t>::from_promise(*this)};
    }

    std::suspend_never initial_suspend()
    {
      return {};
    }

    std::suspend_never final_suspend() noexcept
    {
      return {};
    }

    void unhandled_exception()
    {
      std::terminate();
    }

    void return_value(int v)
    {
      value = v;
    }
  };

  std::coroutine_handle<promise_t> coro;

  int get_value()
  {
    return coro.promise().value;
  }

  ~task_t()
  {
    if (coro)
    {
      coro.destroy();
    }
  }
};

TEST_CASE("Try simple coroutine", "[converter]")
{
}
