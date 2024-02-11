/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#pragma once

#include <uv.h>
#include <vector>
#include <memory>
#include <string>
#include <thread>
#include <mutex>

#include <points/converter/converter.h>

#include "conversion_types.hpp"
#include "event_pipe.hpp"
#include "pre_init_file_retriever.hpp"
#include "reader.hpp"
#include "cache_file_handler.hpp"
#include "threaded_event_loop.hpp"
#include "morton.hpp"
#include "tree_handler.hpp"
#include "attributes_configs.hpp"
#include "input_data_source_registry.hpp"

#include <memory>

namespace points
{
namespace converter
{

struct sorted_input_id_t
{
  input_data_id_t id;
  morton::morton192_t aabb_min;
  bool operator<(const sorted_input_id_t &other) const
  {
    return aabb_min < other.aabb_min;
  }
};

class frustum_tree_walker_t;
class processor_t : public about_to_block_t
{
public:
  processor_t(converter_t &converter);
  void add_files(std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> &&input_files);
  void walk_tree(const std::shared_ptr<frustum_tree_walker_t> &event);
  void about_to_block() override;

  cache_file_handler_t &cache_file() { return _cache_file_handler; }
  const attributes_t &get_attributes(attributes_id_t id);

private:
  converter_t &_converter;
  threaded_event_loop_t _event_loop;

  input_data_source_registry_t _input_data_source_registry;
  cache_file_handler_t _cache_file_handler;
  tree_handler_t _tree_handler;

  event_pipe_t<std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>>> _files_added;

  event_pipe_t<pre_init_info_file_result_t> _pre_init_info_file_result;
  event_pipe_t<file_error_t> _pre_init_file_errors;
  std::vector<std::unique_ptr<get_pre_init_info_worker_t>> _pre_init_info_workers;

  event_pipe_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> _input_init;
  event_pipe_t<input_data_id_t> _sub_added;
  event_pipe_t<std::pair<points_t, error_t>> _sorted_points;
  event_pipe_t<file_error_t> _point_reader_file_errors;
  event_pipe_t<input_data_id_t> _point_reader_done_with_file;

  event_pipe_t<error_t> _cache_file_error;
  event_pipe_t<std::pair<storage_location_t, storage_header_t>> _points_written;
  event_pipe_t<input_data_id_t> _tree_done_with_input;

  threaded_event_loop_t _input_event_loop;
  point_reader_t _point_reader;

  attributes_configs_t _attributes_configs;

  uint32_t _input_sources_inserted_into_tree;

  int64_t _read_sort_budget;
  int64_t _read_sort_active_approximate_size;

  void handle_new_files(std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> &&new_files);


  void handle_pre_init_info_for_files(pre_init_info_file_result_t &&pre_init_info_for_files);
  void handle_file_errors_headers(file_error_t &&error);

  void handle_input_init_done(std::tuple<input_data_id_t, attributes_id_t, header_t> &&event);
  void handle_sub_added(input_data_id_t &&event);
  void handle_sorted_points(std::pair<points_t, error_t> &&event);
  void handle_file_errors(file_error_t &&error);
  void handle_file_reading_done(input_data_id_t &&file);

  void handle_cache_file_error(error_t &&errors);
  void handle_points_written(std::pair<storage_location_t, storage_header_t> &&events);
  void handle_tree_done_with_input(input_data_id_t &&events);

};
}
} // namespace points
