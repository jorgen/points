#include "processor.hpp"

#include "conversion_types.hpp"
#include "converter.hpp"

#include "threaded_event_loop.hpp"

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
  , headers_for_files(event_loop, [this](std::vector<internal_header_t> &&headers) { this->handle_headers(std::move(headers)); })
  , file_errors_headers(event_loop, [this](std::vector<file_error_t> &&events) { this->handle_file_errors_headers(std::move(events)); })
  , sorted_points(event_loop, [this](std::vector<points::converter::points_t> &&events) { this->handle_sorted_points(std::move(events)); })
  , file_errors(event_loop, [this](std::vector<file_error_t> &&events) { this->handle_file_errors(std::move(events)); })
  , point_reader_done_with_file(event_loop, [this](std::vector<input_data_id_t> &&files) { this->handle_file_reading_done(std::move(files));})
  , header_reader(input_event_loop, headers_for_files, file_errors)
  //, point_reader(converter.tree_state, sorted_points, point_reader_done_with_file, file_errors, converter.convert_callbacks)
  , tree_initialized(false)
  , tree_state_initialized(false)
{
  (void) this->converter;
}

void processor_t::add_files(std::vector<input_data_source_t> &&files)
{
  size_t end_index = input_sources.size();
  input_sources.insert(input_sources.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
  std::vector<get_header_files_t> get_header_files;
  get_header_files.reserve(files.size());
  for (size_t i = end_index; i < input_sources.size(); i++)
  {
    using id_type = decltype(input_data_id_t::data);
    input_sources[i].input_id.data = id_type(i); 
    get_header_files.push_back(get_header_files_from_input_data_source(input_sources[i]));
  }
  header_reader.add_files(get_header_files, converter.convert_callbacks);
}

void processor_t::handle_headers(std::vector<internal_header_t> &&headers)
{
  (void)headers;
  if (!tree_state_initialized)
  {
    tree_state_initialized = true;
    //_tr
  }
  //point_reader.add_files(files);
}

void processor_t::handle_file_errors_headers(std::vector<file_error_t> &&errors)
{
  (void)errors;
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

void processor_t::handle_file_reading_done(std::vector<input_data_id_t> &&files)
{
  for (auto &file : files)
  {
    fmt::print(stderr, "Done processing inputfile {}\n", file.data);
  }
}

}
} // namespace points
