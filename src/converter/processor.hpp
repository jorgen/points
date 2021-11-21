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
#include "reader.hpp"
#include "pre_init_file_retriever.hpp"
#include "threaded_event_loop.hpp"
#include "morton.hpp"

#include "tree.hpp"

namespace points
{
namespace converter
{

struct sorted_input_id_t
{
  input_data_id_t id;
  morton::morton64_t aabb_min;
  bool operator<(const sorted_input_id_t &other) const
  {
    return morton::morton_lt(aabb_min, other.aabb_min);
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

  event_pipe_t<points_t> _sorted_points;
  event_pipe_t<file_error_t> _file_errors;
  event_pipe_t<input_data_id_t> _point_reader_done_with_file;

  threaded_event_loop_t _input_event_loop;
  pre_init_file_retriever_t _pre_init_file_retriever;
  point_reader_t _point_reader;

  std::vector<input_data_source_t> _input_sources;

  std::vector<sorted_input_id_t> _pre_init_files_with_aabb_min;
  std::vector<input_data_id_t> _pre_init_no_aabb_min;
  uint32_t _pending_pre_init_files;
  uint32_t _pre_init_files_with_aabb_min_read_index;
  uint32_t _pre_init_files_with_no_aabb_min_read_index;
  uint32_t _read_sort_pending;

  int64_t _read_sort_budget;


  std::vector<sorted_input_id_t> _points_read;
  std::vector<sorted_input_id_t> _points_processed;
  tree_t _tree;
  bool _tree_initialized;
  bool _tree_state_initialized;

  void handle_new_files(std::vector<std::vector<input_data_source_t>> &&new_files);

  void handle_pre_init_info_for_files(std::vector<pre_init_info_file_t> &&pre_init_info_for_files);
  void handle_file_errors_headers(std::vector<file_error_t> &&errors);

  void handle_sorted_points(std::vector<points_t> &&sorted_points);
  void handle_file_errors(std::vector<file_error_t> &&errors);
  void handle_file_reading_done(std::vector<input_data_id_t> &&files);
};
}
} // namespace points