/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

#include "conversion_types.hpp"

#include <ankerl/unordered_dense.h>
#include <optional>

namespace points::converter
{
struct input_data_source_t
{
  input_data_id_t input_id;
  attributes_id_t attribute_id;
  input_name_ref_t name;
  header_t public_header;
};

struct input_data_source_impl_t
{
  input_data_id_t input_id;
  attributes_id_t attribute_id;
  std::unique_ptr<char[]> name;
  uint32_t name_length;
  header_t public_header;
  morton::morton192_t morton_min;
  morton::morton192_t morton_max;
  morton::morton192_t input_order;
  bool read_started = false;
  bool read_finished = false;
  uint8_t approximate_point_size_bytes = 0;
  uint32_t inserted_into_tree = 0;
  uint32_t sub_count = 0;
  uint32_t tree_done_count = 0;
  uint64_t assigned_memory_usage = 0;
  uint64_t approximate_point_count = 0;
  std::vector<storage_location_t> storage_locations;
};

struct input_data_reference_t
{
  input_data_id_t input_id;
  input_name_ref_t name;
};

struct input_data_next_input_t
{
  input_data_id_t id;
  input_name_ref_t name;
  uint8_t approximate_point_size_bytes;
  uint64_t approximate_point_count;
};

class input_data_source_registry_t
{
public:
  input_data_source_registry_t();

  input_data_reference_t register_file(std::unique_ptr<char[]> &&name, uint32_t name_length);
  void register_pre_init_result(const tree_config_t &tree_config, input_data_id_t id, bool found_min, double (&min)[3], uint64_t approximate_point_count, uint8_t approximate_point_size_bytes);
  void handle_input_init(input_data_id_t id, attributes_id_t attributes_id, header_t public_header);
  void handle_sub_added(input_data_id_t id);
  void handle_sorted_points(input_data_id_t id, const morton::morton192_t &min, const morton::morton192_t &max);
  void handle_points_written(input_data_id_t id, std::vector<storage_location_t> &&location);
  void handle_reading_done(input_data_id_t id);
  void handle_tree_done_with_input(input_data_id_t id);
  bool all_inserted_into_tree() const;

  std::optional<input_data_next_input_t> next_input_to_process();

  std::optional<morton::morton192_t> get_done_morton();

  input_data_source_t get(input_data_id_t input_id);

private:
  mutable std::mutex _mutex;
  ankerl::unordered_dense::map<uint32_t, input_data_source_impl_t> _registry;
  uint32_t _input_data_with_sub_parts;
  uint32_t _input_data_inserted_to_tree;
  uint32_t _input_data_id_done_count;
  std::vector<uint32_t> _unsorted_input_sources;
  std::vector<uint32_t> _sorted_input_sources;
  bool _unsorted_input_sources_dirty;
};
} // namespace points::converter
