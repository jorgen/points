#include "processor_p.h"

#include "conversion_types_p.h"
#include "converter_p.h"

#include "threaded_event_loop_p.h"

#include <stdlib.h>
namespace points
{
namespace converter
{

struct thread_count_env_setter_t
{
  thread_count_env_setter_t()
  {
    std::string count = std::to_string(int(std::thread::hardware_concurrency() * 1.5)); 
    //uv_os_setenv("UV_THREADPOOL_SIZE", count.c_str()); 
#if (WIN32)
    _putenv(fmt::format("UV_THREADPOOL_SIZE={}", count.c_str()).c_str()); 
#else
    putenv(fmt::format("UV_THREADPOOL_SIZE={}", count.c_str()).c_str()); 
#endif
  }
};
static thread_count_env_setter_t thread_pool_size_setter;

processor_t::processor_t(converter_t &converter)
  : converter(converter)
  , sorted_points(event_loop, [this](std::vector<points::converter::points_t> &&events) { this->handle_sorted_points(std::move(events)); })
  , file_errors(event_loop, [this](std::vector<file_error_t> &&events) { this->handle_file_errors(std::move(events)); })
  , sorter(sorted_points, file_errors, converter.convert_callbacks)
  , tree_initialized(false)
{
}

void processor_t::add_files(const std::vector<std::string> &files)
{
  sorter.add_files(files);
}

void processor_t::handle_sorted_points(std::vector<points_t> &&sorted_points_event)
{
  if (sorted_points_event.empty())
    return;
  int i = 0;
  if (!tree_initialized)
  {
    tree_initialize(tree, 100000, std::move(sorted_points_event[0]));
    tree_initialized = true;
    i++;
  }

  for (; i < sorted_points_event.size(); i++)
  {
    tree_add_points(tree, std::move(sorted_points_event[i]));
  }
}

void processor_t::handle_file_errors(std::vector<file_error_t> &&errors)
{
  (void)errors;
}

}
} // namespace points
