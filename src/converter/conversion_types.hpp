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

#include <points/converter/converter.h>

#include "morton.hpp"

#include <vector>
#include <memory>
#include <string>

namespace points
{
namespace converter
{

struct input_data_id_t
{
  uint32_t data;
  uint32_t sub;
};
inline bool operator<(const input_data_id_t &a, const input_data_id_t &b)
{
  return a.data < b.data || (a.data == b.data && a.sub < b.sub);
}
inline bool operator==(const input_data_id_t &a, const input_data_id_t &b)
{
  return a.data == b.data && a.sub == b.sub;
}

struct attribute_id_t
{
  uint32_t data;
};

struct input_name_ref_t
{
  const char *name;
  uint32_t name_length;
};


struct attributes_t
{
  std::vector<attribute_t> attributes;
  std::vector<std::unique_ptr<char[]>> attribute_names;
};

struct attribute_buffers_t
{
  std::vector<buffer_t> buffers;
  std::vector<std::unique_ptr<uint8_t[]>> data;
};

struct internal_header_t : header_t
{
  input_data_id_t input_id;
  morton::morton192_t morton_min;
  morton::morton192_t morton_max;
  format_t point_format;
  int lod_span;
};

inline void internal_header_initialize(internal_header_t &header)
{
  header.point_count = 0;
  header.offset[0] = 0.0;
  header.offset[1] = 0.0;
  header.offset[2] = 0.0;
  header.scale[0] = 0.0;
  header.scale[1] = 0.0;
  header.scale[2] = 0.0;
  header.min[0] = -std::numeric_limits<double>::max();
  header.min[1] = -std::numeric_limits<double>::max();
  header.min[2] = -std::numeric_limits<double>::max();
  header.max[0] = std::numeric_limits<double>::max();
  header.max[1] = std::numeric_limits<double>::max();
  header.max[2] = std::numeric_limits<double>::max();

  morton::morton_init_min(header.morton_min);
  morton::morton_init_max(header.morton_max);
  header.lod_span = 255;
}

struct points_t
{
  internal_header_t header;
  attribute_buffers_t buffers;
};

struct input_data_source_t
{
  input_data_id_t input_id;
  attribute_id_t attribute_id;
  std::unique_ptr<char[]> name;
  uint32_t name_length;
  morton::morton192_t min;
  morton::morton192_t max;
  bool read_started;
  bool read_finished;
  bool inserted_into_tree;
  uint8_t approximate_point_size_bytes;
  uint32_t sub_count;
  uint32_t tree_done_count;
  uint64_t approximate_point_count;
  uint64_t assigned_memory_usage;
};

inline input_name_ref_t input_name_ref_from_input_data_source(const input_data_source_t &source)
{
  return {source.name.get(), source.name_length};
}


struct tree_global_state_t
{
  uint32_t node_limit = 100000;
  double scale;
  double inv_scale;
  double offset[3];
};
} // namespace converter
} // namespace points
