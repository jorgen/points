#include "processor.hpp"

#include "conversion_types.hpp"
#include "converter.hpp"

#include "threaded_event_loop.hpp"

#include "morton_tree_coordinate_transform.hpp"

#include <stdlib.h>
#include <algorithm>
#include <functional>

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
  , _cache_file_handler(_converter.tree_state, converter.cache_filename, _attributes_configs, _cache_file_error, _points_written)
  , _tree_handler(_converter.tree_state, _cache_file_handler, _attributes_configs, _tree_done_with_input)
  , _files_added(_event_loop, bind(&processor_t::handle_new_files))
  , _pre_init_info_file_result(_event_loop, bind(&processor_t::handle_pre_init_info_for_files))
  , _pre_init_file_errors(_event_loop, bind(&processor_t::handle_file_errors_headers))
  , _input_init(_event_loop, bind(&processor_t::handle_input_init_done)) 
  , _sub_added(_event_loop, bind(&processor_t::handle_sub_added))
  , _sorted_points(_event_loop, bind(&processor_t::handle_sorted_points))
  , _point_reader_file_errors(_event_loop, bind(&processor_t::handle_file_errors))
  , _point_reader_done_with_file(_event_loop, bind(&processor_t::handle_file_reading_done))
  , _cache_file_error(_event_loop, bind(&processor_t::handle_cache_file_error))
  , _points_written(_event_loop, bind(&processor_t::handle_points_written))
  , _tree_done_with_input(_event_loop, bind(&processor_t::handle_tree_done_with_input))
  , _point_reader(_converter.tree_state, _input_event_loop, _attributes_configs, _input_init, _sub_added, _sorted_points, _point_reader_done_with_file, _point_reader_file_errors)
  , _attributes_configs(_converter.tree_state)
  , _input_sources_inserted_into_tree(0)
  , _read_sort_budget(uint64_t(1) << 20)
  , _read_sort_active_approximate_size(0)
{
  (void) _converter;
  _event_loop.add_about_to_block_listener(this);

}

void processor_t::add_files(std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> &&input_files)
{
  _files_added.post_event(std::move(input_files));
}

void processor_t::walk_tree(const std::shared_ptr<frustum_tree_walker_t> &event)
{
  _tree_handler.walk_tree(event);
}

void processor_t::about_to_block()
{
  while (_read_sort_budget - _read_sort_active_approximate_size > 0)
  {
    auto next_input = _input_data_source_registry.next_input_to_process();
    if (!next_input)
      break;
    _read_sort_active_approximate_size += next_input->approximate_point_count * next_input->approximate_point_size_bytes;
    get_points_file_t file;
    file.callbacks = _converter.convert_callbacks;
    file.id = next_input->id;
    file.filename = next_input->name;
    _point_reader.add_file(std::move(file));
  }
}

const attributes_t& processor_t::get_attributes(attributes_id_t id)
{
  return _attributes_configs.get(id);
}

void processor_t::handle_new_files(std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> &&new_files)
{
  for (auto &new_file : new_files)
  {
    auto input_ref = _input_data_source_registry.register_file(std::move(new_file.first), new_file.second);
    _pre_init_info_workers.emplace_back(new get_pre_init_info_worker_t(_converter.tree_state, input_ref.input_id, input_ref.name, _converter.convert_callbacks, _pre_init_info_file_result, _pre_init_file_errors));
    _pre_init_info_workers.back()->enqueue(_event_loop);
  }
}

void processor_t::handle_pre_init_info_for_files(pre_init_info_file_result_t &&pre_init_for_file)
{
  assert(std::count_if(_pre_init_info_workers.begin(), _pre_init_info_workers.end(), [&](std::unique_ptr<get_pre_init_info_worker_t> &worker) { return worker->input_id == pre_init_for_file.id; }) == 1);
  _pre_init_info_workers.erase(std::find_if(_pre_init_info_workers.begin(), _pre_init_info_workers.end(), [&](std::unique_ptr<get_pre_init_info_worker_t> &worker) { return worker->input_id == pre_init_for_file.id; }));
  _input_data_source_registry.register_pre_init_result(_converter.tree_state, pre_init_for_file.id, pre_init_for_file.found_min, pre_init_for_file.min, pre_init_for_file.approximate_point_count, pre_init_for_file.approximate_point_size_bytes);
}

void processor_t::handle_file_errors_headers(file_error_t &&error)
{
  assert(std::count_if(_pre_init_info_workers.begin(), _pre_init_info_workers.end(), [&](std::unique_ptr<get_pre_init_info_worker_t> &worker) { return worker->input_id == error.input_id; }) == 1);
  _pre_init_info_workers.erase(std::find_if(_pre_init_info_workers.begin(), _pre_init_info_workers.end(), [&](std::unique_ptr<get_pre_init_info_worker_t> &worker) { return worker->input_id == error.input_id; }));
}
void processor_t::handle_input_init_done(std::tuple<input_data_id_t, attributes_id_t, header_t>&& event)
{
  _input_data_source_registry.handle_input_init(std::get<0>(event), std::get<1>(event), std::get<2>(event));
}

void processor_t::handle_sub_added(input_data_id_t&& event)
{
  _input_data_source_registry.handle_sub_added(event);
}

void processor_t::handle_sorted_points(std::pair<points_t,error_t> &&event)
{
  _input_data_source_registry.handle_sorted_points(event.first.header.input_id, event.first.header.morton_min, event.first.header.morton_max);
  _cache_file_handler.write(event.first.header, std::move(event.first.buffers), [](request_id_t, const error_t &) {});
}

void processor_t::handle_file_errors(file_error_t &&errors)
{
  (void)errors;
}

void processor_t::handle_file_reading_done(input_data_id_t &&file)
{
  _input_data_source_registry.handle_reading_done(file);
}

void processor_t::handle_cache_file_error(error_t &&error)
{
  fmt::print(stderr, "Cache file error {} {}\n", error.code, error.msg);
}

void processor_t::handle_points_written(storage_header_t &&event)
{
  if (input_data_id_is_leaf(event.input_id))
    _tree_handler.add_points(std::move(event));
}

void processor_t::handle_tree_done_with_input(input_data_id_t &&event)
{
  _input_data_source_registry.handle_tree_done_with_input(event);
  auto min = _input_data_source_registry.get_done_morton();
  if (min)
    _tree_handler.generate_lod(*min);
}

}
} // namespace points
