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
    std::string env_name = fmt::format("UV_THREADPOOL_SIZE={}", count.c_str());
    char *env_name_c = &env_name[0];
    putenv(env_name_c);
#endif
  }
};
static thread_count_env_setter_t thread_pool_size_setter;

processor_t::processor_t(converter_t &converter)
  : converter(converter)
  , sorted_points(event_loop, [this](std::vector<points::converter::points_t> &&events) { this->handle_sorted_points(std::move(events)); })
  , file_errors(event_loop, [this](std::vector<file_error_t> &&events) { this->handle_file_errors(std::move(events)); })
  , sorter(converter.tree_state, sorted_points, file_errors, converter.convert_callbacks)
  , tree_initialized(false)
  , tree_state_initialized(false)
{
  (void) this->converter;
}

void processor_t::add_files(const std::vector<std::string> &files)
{
  if (!tree_state_initialized)
  {
    if (std::isnan(converter.tree_state.scale[0]) || std::isnan(converter.tree_state.scale[1]) || std::isnan(converter.tree_state.scale[2]))
    {
      if (converter.convert_callbacks.init && files.size())
      {
        internal_header_t header;
        error_t *local_error = nullptr;
        void *user_ptr;
        converter.convert_callbacks.init(files[0].c_str(), files[0].size(), &header, &user_ptr, &local_error);
        if (local_error)
        {
          file_error_t file_error;
          file_error.filename = files[0];
          file_error.error = *local_error;
          file_errors.post_event(std::move(file_error));
        }
        if (converter.convert_callbacks.destroy_user_ptr)
        {
          converter.convert_callbacks.destroy_user_ptr(user_ptr);
          user_ptr = nullptr;
        }
        static_assert(std::is_same<decltype(converter.tree_state.scale), decltype(header.scale)>::value, "Scale types are not equal size");
        memcpy(converter.tree_state.scale, header.scale, sizeof(header.scale));
      }
    }
    if (std::isnan(converter.tree_state.scale[0]) || std::isnan(converter.tree_state.scale[1]) || std::isnan(converter.tree_state.scale[2]))
    {
      converter.tree_state.scale[0] = 0.01;
      converter.tree_state.scale[1] = 0.01;
      converter.tree_state.scale[2] = 0.01;
      if (converter.runtime_callbacks.warning)
        converter.runtime_callbacks.warning("Failed to initialize tree scale factor, falling back to 0.001");
    }
    if (std::isnan(converter.tree_state.offset[0]) || std::isnan(converter.tree_state.offset[1]) || std::isnan(converter.tree_state.offset[2]))
    {
      converter.tree_state.offset[0] = -double(uint32_t(1) << 17);
      converter.tree_state.offset[1] = -double(uint32_t(1) << 17);
      converter.tree_state.offset[2] = -double(uint32_t(1) << 17);
    }
    tree_state_initialized = true;
  }
  sorter.add_files(files);
}

void processor_t::handle_sorted_points(std::vector<points_t> &&sorted_points_event)
{
  if (sorted_points_event.empty())
    return;
  int i = 0;
  if (!tree_initialized)
  {
    tree_initialize(converter.tree_state, tree, std::move(sorted_points_event[0]));
    tree_initialized = true;
    i++;
  }

  for (; i < int(sorted_points_event.size()); i++)
  {
    tree_add_points(converter.tree_state, tree, std::move(sorted_points_event[i]));
  }
}

void processor_t::handle_file_errors(std::vector<file_error_t> &&errors)
{
  (void)errors;
}

}
} // namespace points
