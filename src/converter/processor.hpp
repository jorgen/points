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

class processor_t : public about_to_block_t
{
public:
  processor_t(converter_t &converter);
  void add_files(std::vector<input_data_source_t> &&files);
  void about_to_block() override;
  //void add_data(const void *data, size_t data_size);

private:
  converter_t &_converter;
  threaded_event_loop_t _event_loop;
 
  event_pipe_t<std::vector<input_data_source_t>> _files_added;

  event_pipe_t<pre_init_info_file_t> _pre_init_for_files;
  event_pipe_t<file_error_t> _pre_init_file_errors;

  event_pipe_t<std::pair<input_data_id_t, attributes_t>> _attributes_for_source;
  event_pipe_t<std::pair<points_t, error_t>> _sorted_points;
  event_pipe_t<file_error_t> _point_reader_file_errors;
  event_pipe_t<input_data_id_t> _point_reader_done_with_file;

  event_pipe_t<error_t> _cache_file_error;
  event_pipe_t<internal_header_t> _points_written;
  event_pipe_t<input_data_id_t> _tree_done_with_input;

  threaded_event_loop_t _input_event_loop;
  pre_init_file_retriever_t _pre_init_file_retriever;
  point_reader_t _point_reader;

  cache_file_handler_t _cache_file_handler;
  tree_handler_t _tree_handler;

  std::vector<input_data_source_t> _input_sources;
  attributes_configs_t _attributes_configs;

  std::vector<sorted_input_id_t> _processing_order;
  uint32_t _pending_pre_init_files;
  uint32_t _pre_init_files_read_index;
  uint32_t _tree_lod_generate_until_index;
  uint32_t _input_sources_inserted_into_tree;

  int64_t _read_sort_budget;

  void handle_new_files(std::vector<std::vector<input_data_source_t>> &&new_files);

  void handle_pre_init_info_for_files(std::vector<pre_init_info_file_t> &&pre_init_info_for_files);
  void handle_file_errors_headers(std::vector<file_error_t> &&errors);

  void handle_attribute_event(std::vector<std::pair<input_data_id_t, attributes_t>> &&attribute_events);
  void handle_sorted_points(std::vector<std::pair<points_t,error_t>> &&sorted_points);
  void handle_file_errors(std::vector<file_error_t> &&errors);
  void handle_file_reading_done(std::vector<input_data_id_t> &&files);

  void handle_cache_file_error(std::vector<error_t> &&errors);
  void handle_points_written(std::vector<internal_header_t> &&events);
  void handle_tree_done_with_input(std::vector<input_data_id_t> &&events);

};
}
} // namespace points
