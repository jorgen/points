#include "processor.hpp"

#include "conversion_types.hpp"
#include "converter.hpp"

#include "threaded_event_loop.hpp"

#include "morton_tree_coordinate_transform.hpp"

#include <stdlib.h>
#include <algorithm>

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
  : _converter(converter)
  , _files_added(_event_loop, [this](std::vector<std::vector<input_data_source_t>> &&new_files) { this->handle_new_files(std::move(new_files)); })
  , _pre_init_for_files(_event_loop, [this](std::vector<pre_init_info_file_t> &&aabb_min_for_files) { this->handle_pre_init_info_for_files(std::move(aabb_min_for_files)); })
  , _pre_init_file_errors(_event_loop, [this](std::vector<file_error_t> &&events) { this->handle_file_errors_headers(std::move(events)); })
  , _sorted_points(_event_loop, [this](std::vector<points::converter::points_t> &&events) { this->handle_sorted_points(std::move(events)); })
  , _file_errors(_event_loop, [this](std::vector<file_error_t> &&events) { this->handle_file_errors(std::move(events)); })
  , _point_reader_done_with_file(_event_loop, [this](std::vector<input_data_id_t> &&files) { this->handle_file_reading_done(std::move(files));})
  , _pre_init_file_retriever(_converter.tree_state, _input_event_loop, _pre_init_for_files, _file_errors)
  , _point_reader(_converter.tree_state, _input_event_loop, _sorted_points, _point_reader_done_with_file, _file_errors)
  , _pending_pre_init_files(0)
  , _pre_init_files_with_aabb_min_read_index(0)
  , _pre_init_files_with_no_aabb_min_read_index(0)
  , _read_sort_pending(0)
  , _read_sort_budget(uint64_t(1) << 20)
  , _tree_initialized(false)
{
  (void) _converter;
  _event_loop.add_about_to_block_listener(this);
}

void processor_t::add_files(std::vector<input_data_source_t> &&files)
{
  _files_added.post_event(std::move(files));
}

void processor_t::about_to_block()
{
  if (_pending_pre_init_files == 0)
  { 
    //&& _aabb_min_read_index < _aabb_min_read.size())
    bool no_more_input_files = false;
    for (;!no_more_input_files && _pre_init_files_with_no_aabb_min_read_index < _pre_init_no_aabb_min.size() && _read_sort_budget > 0; _pre_init_files_with_no_aabb_min_read_index++)
    {
      auto &input_file = _input_sources[_pre_init_no_aabb_min[_pre_init_files_with_no_aabb_min_read_index].data]; 
      if (input_file.read_started)
        continue;
      if (input_file.approximate_point_count == 0 || input_file.approximate_point_size_bytes)
      {
        no_more_input_files = true;
        if (_read_sort_pending > 0)
          break;
      }
      auto approximate_input_size = input_file.approximate_point_count * input_file.approximate_point_size_bytes;
      _read_sort_budget -= int64_t(approximate_input_size);
      _read_sort_pending++;
      //_point_reader.add_file()
    }
//    for (; _headers_read_index<_headers_read.size() && _read_sort_budget> 0; _headers_read_index++)
//    {
//      auto &input_file = _input_sources[_headers_read[_headers_read_index].id.data]; 
//      assert(input_file.header_started && input_file.header_finished);
//      if (input_file.read_started)
//        continue;
//      auto expected_input_size = header_expected_input_size(input_file.header);
//      _read_sort_budget -= int64_t(expected_input_size);
//    }
  }
}

void processor_t::handle_new_files(std::vector<std::vector<input_data_source_t>> &&new_files_collection)
{
  for (auto &new_files : new_files_collection)
  {
    auto new_files_size = new_files.size();
    //_headers_read_pending += uint32_t(new_files_size);
    size_t end_index = _input_sources.size();
    _input_sources.insert(_input_sources.end(), std::make_move_iterator(new_files.begin()), std::make_move_iterator(new_files.end()));
    std::vector<get_file_pre_init_t> get_pre_init_files;
    get_pre_init_files.reserve(new_files_size);
    for (size_t i = end_index; i < _input_sources.size(); i++)
    {
      using id_type = decltype(input_data_id_t::data);
      auto &source = _input_sources[i];
      source.input_id.data = id_type(i);
      source.read_started = false;
      source.read_finished = false;
      source.approximate_point_count = 0;
      source.approximate_point_size_bytes = 0;
      get_pre_init_files.push_back(get_file_pre_init_from_input_data_source(source));
    }
    _pre_init_file_retriever.add_files(get_pre_init_files, _converter.convert_callbacks);
  }
}

void processor_t::handle_pre_init_info_for_files(std::vector<pre_init_info_file_t> &&pre_init_for_files)
{
  _pending_pre_init_files -= uint32_t(pre_init_for_files.size());
  for (auto &&pre_init_for_file : pre_init_for_files)
  {
    auto &source = _input_sources[pre_init_for_file.id.data];
    source.approximate_point_count = pre_init_for_file.approximate_point_count;
    source.approximate_point_size_bytes = pre_init_for_file.approximate_point_size_bytes;
    if (pre_init_for_file.found_min)
    {
      _pre_init_files_with_aabb_min.emplace_back();
      auto &back = _pre_init_files_with_aabb_min.back();
      back.id = pre_init_for_file.id;
      convert_pos_to_morton(_converter.tree_state.scale, _converter.tree_state.offset, pre_init_for_file.min, back.aabb_min);

    }
    else
    {
      _pre_init_no_aabb_min.emplace_back(pre_init_for_file.id);
    }
  }
  if (_pending_pre_init_files == 0)
  {
    std::sort(_pre_init_files_with_aabb_min.begin(), _pre_init_files_with_aabb_min.end());
    _pre_init_files_with_aabb_min_read_index = 0;
  }
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
  if (!_tree_initialized)
  {
    tree_initialize(_converter.tree_state, _tree, std::move(sorted_points_event[0]));
    _tree_initialized = true;
    i++;
  }

  for (; i < int(sorted_points_event.size()); i++)
  {
    tree_add_points(_converter.tree_state, _tree, std::move(sorted_points_event[i]));
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
