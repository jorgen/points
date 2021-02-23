#include "processor_p.h"

#include "converter_p.h"

#include "threaded_event_loop_p.h"

#include "sorter_p.h"

namespace points
{
namespace converter
{

struct uv_thread_count_env_setter
{
  uv_thread_count_env_setter()
  {
    std::string count = std::to_string(int(std::thread::hardware_concurrency() * 1.5)); 
    uv_os_setenv("UV_THREADPOOL_SIZE", count.c_str()); 
  }
};
static uv_thread_count_env_setter thread_pool_size_setter;

processor_t::processor_t(converter_t &converter)
  : converter(converter)
  , sorted_points(event_loop, [this](std::vector<points::converter::points_t> &&events) { this->handle_sorted_points(std::move(events)); })
  , file_errors(event_loop, [this](std::vector<error_t> &&events) { this->handle_file_errors(std::move(events)); })
  , sorter(sorted_points, file_errors, converter.convert_callbacks)
{
}

void processor_t::add_files(const std::vector<std::string> &files)
{
  sorter.add_files(files);
}

void processor_t::handle_sorted_points(std::vector<points_t> &&sorted_points_event)
{
  (void)sorted_points_event;
}

void processor_t::handle_file_errors(std::vector<error_t> &&errors)
{
  (void)errors;
}

}
} // namespace points
