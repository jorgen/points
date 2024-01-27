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

#include "error.hpp"
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
inline bool operator!=(const input_data_id_t &a, const input_data_id_t &b)
{
  return !(a == b);
}

inline bool input_data_id_is_leaf(input_data_id_t input)
{
  return !(input.sub & decltype(input.sub)(1) << 31);
}
struct file_error_t
{
  input_data_id_t input_id;
  error_t error;
};

struct attributes_id_t
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

struct storage_header_t
{
  input_data_id_t input_id;
  attributes_id_t attributes_id;
  uint64_t point_count;
  std::pair<type_t, components_t> point_format;
  morton::morton192_t morton_min;
  morton::morton192_t morton_max;
  int lod_span;
};

inline void storage_header_initialize(storage_header_t &header)
{
  header.point_count = 0;

  morton::morton_init_min(header.morton_max);
  morton::morton_init_max(header.morton_min);
  header.lod_span = 255;
}

struct points_t
{
  storage_header_t header;
  attribute_buffers_t buffers;
};

struct tree_global_state_t
{
  uint64_t node_limit = 100000;
  double scale;
  double inv_scale;
  double offset[3];
};

struct offset_t
{
  explicit offset_t(uint64_t data) : data(data){}
  offset_t() : data(0) {}
  uint64_t data;
};
struct index_t
{
  explicit index_t(uint32_t data) : data(data) { }
  uint32_t data;
};
struct point_count_t
{
  explicit point_count_t(uint32_t data) : data(data){}
  explicit point_count_t() {}
  uint32_t data;
};

struct points_subset_t
{
  points_subset_t(input_data_id_t id, offset_t offset, point_count_t count)
    : input_id(id)
    , offset(offset)
    , count(count)
  {}
  input_data_id_t input_id;
  offset_t offset;
  point_count_t count;
};

} // namespace converter
} // namespace points
