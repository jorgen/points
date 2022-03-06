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
  , _attributes_for_source(_event_loop, [this](std::vector<std::pair<input_data_id_t, attributes_t>> &&events) { this->handle_attribute_event(std::move(events)); })
  , _sorted_points(_event_loop, [this](std::vector<std::pair<points::converter::points_t,error_t>> &&events) { this->handle_sorted_points(std::move(events)); })
  , _point_reader_file_errors(_event_loop, [this](std::vector<file_error_t> &&events) { this->handle_file_errors(std::move(events)); })
  , _point_reader_done_with_file(_event_loop, [this](std::vector<input_data_id_t> &&files) { this->handle_file_reading_done(std::move(files));})
  , _cache_file_error(_event_loop, [this](std::vector<error_t> &&errors) { this->handle_cache_file_error(std::move(errors));})
  , _points_written(_event_loop, [this](std::vector<internal_header_t> &&events) {this->handle_points_written(std::move(events)); })
  , _pre_init_file_retriever(_converter.tree_state, _input_event_loop, _pre_init_for_files, _point_reader_file_errors)
  , _point_reader(_converter.tree_state, _input_event_loop, _attributes_for_source, _sorted_points, _point_reader_done_with_file, _point_reader_file_errors)
  , _cache_file_handler(_converter.tree_state, converter.cache_filename, _cache_file_error, _points_written)
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
    bool can_process_more = true;
    //&& _aabb_min_read_index < _aabb_min_read.size())
    for (;can_process_more && _pre_init_files_with_no_aabb_min_read_index < _pre_init_no_aabb_min.size() && _read_sort_budget > 0; _pre_init_files_with_no_aabb_min_read_index++)
    {
      auto &input_file = _input_sources[_pre_init_no_aabb_min[_pre_init_files_with_no_aabb_min_read_index].data]; 
      if (input_file.read_started)
        continue;
      if (input_file.approximate_point_count == 0 || input_file.approximate_point_size_bytes == 0)
      {
        if (_read_sort_pending > 0)
        {
          can_process_more = false;
          break;
        }
        input_file.assigned_memory_usage = uint64_t(1) << 50;
      }
      else
      {
        auto approximate_input_size = input_file.approximate_point_count * input_file.approximate_point_size_bytes;
        input_file.assigned_memory_usage = approximate_input_size;
      }

      input_file.read_started = true;
      _read_sort_budget -= input_file.assigned_memory_usage;
      _read_sort_pending++;
      get_points_file_t file;
      file.callbacks = _converter.convert_callbacks;
      file.id = input_file.input_id;
      file.filename = input_name_ref_from_input_data_source(input_file);
      _point_reader.add_file(std::move(file));
    }

    for (; can_process_more && _pre_init_files_with_aabb_min_read_index < _pre_init_files_with_aabb_min.size() && _read_sort_budget > 0; _pre_init_files_with_aabb_min_read_index++)
    {
      auto &input_file = _input_sources[_pre_init_files_with_aabb_min[_pre_init_files_with_aabb_min_read_index].id.data]; 
      if (input_file.read_started)
        continue;
      auto approximate_input_size = input_file.approximate_point_count * input_file.approximate_point_size_bytes;
      input_file.assigned_memory_usage = approximate_input_size;
      input_file.read_started = true;
      _read_sort_budget -= input_file.assigned_memory_usage;
      _read_sort_pending++;
      get_points_file_t file;
      file.callbacks = _converter.convert_callbacks;
      file.id = input_file.input_id;
      file.filename = input_name_ref_from_input_data_source(input_file);
      _point_reader.add_file(std::move(file));
    }
  }
}

void processor_t::handle_new_files(std::vector<std::vector<input_data_source_t>> &&new_files_collection)
{
  for (auto &new_files : new_files_collection)
  {
    _pending_pre_init_files += uint32_t(new_files.size());
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
      source.input_id.sub = 0;
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

static bool compare_attribute(const attribute_t &a, const attribute_t &b)
{
  if (a.name_size != b.name_size)
    return false;
  if (a.format != b.format)
    return false;
  if (a.group != b.group)
    return false;
  if (a.components != b.components)
    return false;
  return memcmp(a.name, b.name, a.name_size) == 0;
}
static bool compare_attributes(const attributes_t &a, const attributes_t &b)
{
  if (a.attributes.size() != b.attributes.size())
    return false;
  for (int i = 1; i < int(a.attributes.size()); i++)
  {
    if (!compare_attribute(a.attributes[i], b.attributes[i]))
      return false;
  }
  return true;
}

static int get_attribute_config_index(std::vector<std::unique_ptr<attributes_t>> &attribute_configs, attributes_t &&find)
{
  for (int i = 0; i < int(attribute_configs.size()); i++)
  {
    auto &source = attribute_configs[i];
    if (compare_attributes(*source, find))
      return i;
  }

  int ret = int(attribute_configs.size());
  //attribute_configs.emplace_back(std::move(find));
  attribute_configs.emplace_back(new attributes_t());
  auto &back = attribute_configs.back();
  *back = std::move(find);
  return ret;
}

void processor_t::handle_attribute_event(std::vector<std::pair<input_data_id_t, attributes_t>> &&attribute_events)
{
  for (auto &event : attribute_events)
  {
    _input_sources[event.first.data].attribute_id.data = get_attribute_config_index(_attributes_configs, std::move(event.second));
  }
}
void processor_t::handle_sorted_points(std::vector<std::pair<points_t,error_t>> &&sorted_points_event)
{
  for(auto &event : sorted_points_event)
  {
    auto attributes = _attributes_configs[_input_sources[event.first.header.input_id.data].attribute_id.data].get();
    _cache_file_handler.write(event.first.header, std::move(event.first.buffers), attributes);
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
    auto &source = _input_sources[file.data];
    fmt::print(stderr, "Done processing inputfile {}\n", source.name.get());
  }
}

void processor_t::handle_cache_file_error(std::vector<error_t> &&errors)
{
  for (auto &error : errors)
  {
    fmt::print(stderr, "Cache file error {} {}\n", error.code, error.msg); 
  }
}

void processor_t::handle_points_written(std::vector<internal_header_t> &&events)
{
  for (auto &event : events)
  {
    if (!_tree_initialized)
    {
      _tree_initialized = true;
      tree_initialize(_converter.tree_state, _cache_file_handler, _tree, event);
    }
    else
    {
      tree_add_points(_converter.tree_state, _cache_file_handler, _tree, event);
    }
    fmt::print(stderr, "written {} {}\n", event.input_id.data, event.input_id.sub);
  }
}

}
} // namespace points
