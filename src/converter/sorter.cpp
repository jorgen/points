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
#include "sorter.hpp"
#include "attributes_configs.hpp"
#include "error.hpp"
#include "morton.hpp"
#include "morton_tree_coordinate_transform.hpp"

#include <points/converter/default_attribute_names.h>

#include "type_from_type.hpp"
#include <limits>
#include <numeric>
#include <vector>

#include <fmt/printf.h>

#include <assert.h>

namespace points::converter
{
template <typename T>
struct vec_t
{
  T data[3];
};

template <typename INDEX_T, typename T1, size_t C1, typename T2, size_t C2>
typename std::enable_if<(sizeof(morton::morton_t<T1, C1>) > sizeof(morton::morton_t<T2, C2>))>::type downcast_point_buffer(const INDEX_T *indecies_begin, const std::unique_ptr<uint8_t[]> &source, uint32_t source_size,
                                                                                                                           std::unique_ptr<uint8_t[]> &target, uint32_t &target_size)
{
  uint32_t point_count = source_size / sizeof(morton::morton_t<T1, C1>);
  target_size = point_count * sizeof(morton::morton_t<T2, C2>);
  target.reset(new uint8_t[target_size]);
  const morton::morton_t<T1, C1> *source_morton = reinterpret_cast<const morton::morton_t<T1, C1> *>(source.get());
  morton::morton_t<T2, C2> *target_morton = reinterpret_cast<morton::morton_t<T2, C2> *>(target.get());
  for (uint32_t i = 0; i < point_count; i++)
  {
    morton::morton_downcast(source_morton[indecies_begin[i]], target_morton[i]);
  }
}

template <typename INDEX_T, typename T1, size_t C1, typename T2, size_t C2>
typename std::enable_if<(sizeof(morton::morton_t<T1, C1>) <= sizeof(morton::morton_t<T2, C2>))>::type downcast_point_buffer(const INDEX_T *indecies_begin, const std::unique_ptr<uint8_t[]> &source, uint32_t source_size,
                                                                                                                            std::unique_ptr<uint8_t[]> &target, uint32_t &target_size)
{
  (void)indecies_begin;
  (void)source;
  (void)source_size;
  (void)target;
  (void)target_size;
}

bool fuzzy_comp(const int (&a)[3], const int (&b)[3])
{
  for (int i = 0; i < 3; i++)
  {
    int n = a[i];
    int m = b[i];
    if (m < n)
      std::swap(n, m);
    if (n < m - 1)
      return false;
    if (m > n + 1)
      return false;
  }
  return true;
}

template <typename index_iterator, typename value_iterator>
void reorder_into_target(uint32_t count, const index_iterator indecies, const value_iterator value, value_iterator target)
{
  for (uint64_t i = 0; i < count; i++)
  {
    target[i] = value[indecies[i]];
  }
}

template <typename order_iterator, typename value_iterator>
void reorderVectorInPlace(order_iterator indecies_begin, order_iterator indecies_end, value_iterator values)
{
  size_t count = indecies_end - indecies_begin;
  std::vector<bool> visited(count, false);

  for (size_t i = 0; i < count; ++i)
  {
    // If it's already visited, skip
    if (visited[i])
      continue;

    auto nextIndex = indecies_begin[i];
    auto valueToPlace = values[i];

    while (!visited[nextIndex])
    {
      visited[nextIndex] = true;

      // Swap the value to place with the value at the target index
      auto temp = values[nextIndex];
      values[nextIndex] = valueToPlace;
      valueToPlace = temp;

      nextIndex = indecies_begin[nextIndex];
    }
  }
}

template <typename INDEX_T, size_t C, typename T>
void reorder_buffer_two(uint32_t count, const INDEX_T *indecies_begin, const void *source, std::unique_ptr<uint8_t[]> &target, uint32_t &target_size)
{
  using copy_t = std::array<T, C>;
  const copy_t *source_data = reinterpret_cast<const copy_t *>(source);
  target_size = count * sizeof(copy_t);
  target.reset(new uint8_t[target_size]);
  copy_t *target_data = reinterpret_cast<copy_t *>(target.get());
  for (uint32_t i = 0; i < count; i++)
  {
    memcpy(target_data + i, source_data + indecies_begin[i], sizeof(copy_t));
  }
}

template <typename INDEX_T, size_t C>
void reorder_buffer_one(uint32_t count, const INDEX_T *indecies_begin, std::pair<type_t, components_t> format, const void *source, std::unique_ptr<uint8_t[]> &target, uint32_t &target_size)
{
  switch (format.first)
  {
  case type_u8:
    reorder_buffer_two<INDEX_T, C, uint8_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_i8:
    reorder_buffer_two<INDEX_T, C, int8_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_u16:
    reorder_buffer_two<INDEX_T, C, uint16_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_i16:
    reorder_buffer_two<INDEX_T, C, int16_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_u32:
    reorder_buffer_two<INDEX_T, C, uint32_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_i32:
    reorder_buffer_two<INDEX_T, C, int32_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_m32:
    reorder_buffer_two<INDEX_T, C, morton::morton32_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_r32:
    reorder_buffer_two<INDEX_T, C, float>(count, indecies_begin, source, target, target_size);
    break;
  case type_u64:
    reorder_buffer_two<INDEX_T, C, uint64_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_i64:
    reorder_buffer_two<INDEX_T, C, int64_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_m64:
    reorder_buffer_two<INDEX_T, C, morton::morton64_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_r64:
    reorder_buffer_two<INDEX_T, C, double>(count, indecies_begin, source, target, target_size);
    break;
  case type_m128:
    reorder_buffer_two<INDEX_T, C, morton::morton128_t>(count, indecies_begin, source, target, target_size);
    break;
  case type_m192:
    reorder_buffer_two<INDEX_T, C, morton::morton192_t>(count, indecies_begin, source, target, target_size);
    break;
  }
}

template <typename INDEX_T>
static void reorder_buffer(uint32_t count, const INDEX_T *indecies_begin, std::pair<type_t, components_t> format, const void *source, std::unique_ptr<uint8_t[]> &target, uint32_t &target_size)
{
  switch (format.second)
  {
  case components_1:
    reorder_buffer_one<INDEX_T, 1>(count, indecies_begin, format, source, target, target_size);
    break;
  case components_2:
    reorder_buffer_one<INDEX_T, 2>(count, indecies_begin, format, source, target, target_size);
    break;
  case components_3:
    reorder_buffer_one<INDEX_T, 3>(count, indecies_begin, format, source, target, target_size);
    break;
  case components_4:
    reorder_buffer_one<INDEX_T, 4>(count, indecies_begin, format, source, target, target_size);
    break;
  case components_4x4:
    reorder_buffer_one<INDEX_T, 16>(count, indecies_begin, format, source, target, target_size);
    break;
  }
}

template <typename T, typename INDEX_T, typename MT, size_t C>
void convert_and_sort_morton(const tree_config_t &tree_config, attributes_configs_t &attributes_config, const header_t &public_header, points_t &points, double smallest_scale, type_t type, error_t &error)
{
  (void)error;
  auto &header = points.header;
  auto count = header.point_count;
  auto buffer_size = uint32_t(sizeof(morton::morton_t<MT, C>) * count);
  std::unique_ptr<uint8_t[]> world_morton_unique_ptr(new uint8_t[buffer_size]);
  morton::morton_t<MT, C> *morton_begin = reinterpret_cast<morton::morton_t<MT, C> *>(world_morton_unique_ptr.get());
  uint64_t tmp[3];
  const vec_t<T> *point_data = reinterpret_cast<const vec_t<T> *>(points.buffers.buffers[0].data);

  bool scale_is_same = smallest_scale == public_header.scale[0] && smallest_scale == public_header.scale[1] && smallest_scale == public_header.scale[2];

  if (scale_is_same)
  {
    double inv_scale = 1 / smallest_scale;

    int64_t local_offset_diff[3];
    local_offset_diff[0] = -int64_t(public_header.offset[0] * inv_scale) + int64_t(tree_config.offset[0] * inv_scale);
    local_offset_diff[1] = -int64_t(public_header.offset[1] * inv_scale) + int64_t(tree_config.offset[1] * inv_scale);
    local_offset_diff[2] = -int64_t(public_header.offset[2] * inv_scale) + int64_t(tree_config.offset[2] * inv_scale);

    for (uint64_t i = 0; i < count; i++)
    {
      auto &point = point_data[i];
      tmp[0] = int64_t(point.data[0]) - local_offset_diff[0];
      tmp[1] = int64_t(point.data[1]) - local_offset_diff[1];
      tmp[2] = int64_t(point.data[2]) - local_offset_diff[2];
      morton::encode(tmp, morton_begin[i]);
    }
  }
  else
  {
    double inv_scale = 1 / smallest_scale;
    for (uint64_t i = 0; i < count; i++)
    {
      auto &point = point_data[i];
      tmp[0] = int64_t((double(point.data[0] * public_header.scale[0]) + public_header.offset[0] - tree_config.offset[0]) * inv_scale);
      tmp[1] = int64_t((double(point.data[1] * public_header.scale[1]) + public_header.offset[1] - tree_config.offset[1]) * inv_scale);
      tmp[2] = int64_t((double(point.data[2] * public_header.scale[2]) + public_header.offset[2] - tree_config.offset[2]) * inv_scale);
      morton::encode(tmp, morton_begin[i]);
    }
  }
  std::unique_ptr<uint8_t[]> indecies(new uint8_t[sizeof(INDEX_T) * count]);
  INDEX_T *indecies_begin = reinterpret_cast<INDEX_T *>(indecies.get());
  INDEX_T *indecies_end = indecies_begin + count;
  std::iota(indecies_begin, indecies_end, INDEX_T(0));

  std::stable_sort(indecies_begin, indecies_end, [morton_begin](INDEX_T a, INDEX_T b) { return morton_begin[a] < morton_begin[b]; });

  morton::morton192_t base_morton;
  morton::encode(tmp, base_morton);

  morton::morton_t<MT, C> first = morton_begin[indecies_begin[0]];
  morton::morton_t<MT, C> last = morton_begin[indecies_begin[count - 1]];
  assert(first < last);
  points.header.lod_span = morton::morton_lod(first, last);
  type_t new_type = morton_type_from_lod(points.header.lod_span);
  std::unique_ptr<uint8_t[]> new_data;
  uint32_t new_buffer_size = 0;
  if (new_type == type_m32 && sizeof(morton::morton_t<MT, C>) > sizeof(morton::morton32_t))
    downcast_point_buffer<INDEX_T, MT, C, uint32_t, 1>(indecies_begin, world_morton_unique_ptr, buffer_size, new_data, new_buffer_size);
  else if (new_type == type_m64 && sizeof(morton::morton_t<MT, C>) > sizeof(morton::morton64_t))
    downcast_point_buffer<INDEX_T, MT, C, uint64_t, 1>(indecies_begin, world_morton_unique_ptr, buffer_size, new_data, new_buffer_size);
  else if (new_type == type_m128 && sizeof(morton::morton_t<MT, C>) > sizeof(morton::morton128_t))
    downcast_point_buffer<INDEX_T, MT, C, uint64_t, 2>(indecies_begin, world_morton_unique_ptr, buffer_size, new_data, new_buffer_size);
  else
  {
    assert(type == new_type);
    reorder_buffer<INDEX_T>(count, indecies_begin, std::make_pair(new_type, components_1), world_morton_unique_ptr.get(), new_data, new_buffer_size);
  }

  world_morton_unique_ptr = std::move(new_data);
  buffer_size = new_buffer_size;
  type = new_type;

  points.buffers.data[0] = std::move(world_morton_unique_ptr);
  points.buffers.buffers[0].data = points.buffers.data[0].get();
  points.buffers.buffers[0].size = buffer_size;

  auto &orig_attributes = attributes_config.get(points.attributes_id);
  attributes_t attributes;
  attributes_copy(orig_attributes, attributes);
  attributes.attributes[0].type = type;
  attributes.attribute_names.push_back(std::unique_ptr<char[]>(new char[sizeof(POINTS_ATTRIBUTE_ORIGINAL_ORDER) + 1]));
  memcpy(attributes.attribute_names.back().get(), POINTS_ATTRIBUTE_ORIGINAL_ORDER, sizeof(POINTS_ATTRIBUTE_ORIGINAL_ORDER));
  attributes.attribute_names.back().get()[sizeof(POINTS_ATTRIBUTE_ORIGINAL_ORDER)] = 0;
  attributes.attributes.emplace_back(attributes.attribute_names.back().get(), uint32_t(sizeof(POINTS_ATTRIBUTE_ORIGINAL_ORDER)), type_from_type<INDEX_T>(), components_1);
  for (int i = 1; i < int(attributes.attributes.size()) - 1; i++)
  {
    std::unique_ptr<uint8_t[]> new_attr_data;
    auto &attr = attributes.attributes[i];
    auto attr_format = std::make_pair(attr.type, attr.components);
    reorder_buffer<INDEX_T>(count, indecies_begin, attr_format, points.buffers.data[i].get(), new_attr_data, new_buffer_size);
    points.buffers.data[i] = std::move(new_attr_data);
    assert(new_buffer_size == points.buffers.buffers[i].size);

    points.buffers.buffers[i].data = points.buffers.data[i].get();
  }
  points.buffers.data.emplace_back(std::move(indecies));
  points.buffers.buffers.emplace_back(points.buffers.data.back().get(), uint32_t(sizeof(INDEX_T)) * count);

  points.attributes_id = attributes_config.get_attribute_config_index(std::move(attributes));
  points.header.point_format = {type, components_1};
  morton::morton_upcast(first, base_morton, points.header.morton_min);
  morton::morton_upcast(last, base_morton, points.header.morton_max);
  assert(points.header.lod_span == morton::morton_lod(points.header.morton_min, points.header.morton_max));
}

template <typename T, typename INDEX_T>
void convert_and_sort(const tree_config_t &tree_config, attributes_configs_t &attributes_configs, const header_t &public_header, points_t &points, error_t &error)
{
  auto &header = points.header;

  fmt::print(stderr, "Sorting {} - {}\n", points.header.input_id.data, points.header.input_id.sub);
  double smallest_scale = tree_config.scale;

  morton::morton192_t global_min = {};
  morton::morton192_t global_max;
  global_max.data[0] = ~uint64_t(0);
  global_max.data[1] = ~uint64_t(0);
  global_max.data[2] = ~uint64_t(0);
  double global_min_pos[3];
  double global_max_pos[3];
  convert_morton_to_pos(smallest_scale, tree_config.offset, global_min, global_min_pos);
  convert_morton_to_pos(smallest_scale, tree_config.offset, global_max, global_max_pos);

  type_t target_format;
  if (public_header.min[0] == -std::numeric_limits<double>::max() && public_header.min[1] == -std::numeric_limits<double>::max() && public_header.min[2] == -std::numeric_limits<double>::max() &&
      public_header.max[0] == std::numeric_limits<double>::max() && public_header.min[1] == std::numeric_limits<double>::max() && public_header.min[2] == std::numeric_limits<double>::max())
  {
    target_format = type_t::type_m192;
  }
  else if (public_header.min[0] < global_min_pos[0] || public_header.min[1] < global_min_pos[1] || public_header.min[2] < global_min_pos[2] || global_max_pos[0] < public_header.max[0] ||
           global_max_pos[1] < public_header.max[1] || global_max_pos[2] < public_header.max[2])
  {
    error.msg = "Data for is outside the dataset range.";
    error.code = -1;
    return;
  }
  else
  {
    convert_pos_to_morton(smallest_scale, tree_config.offset, public_header.min, header.morton_min);
    convert_pos_to_morton(smallest_scale, tree_config.offset, public_header.max, header.morton_max);
    int lod = morton::morton_lod(header.morton_min, header.morton_max);
    target_format = morton_type_from_lod(lod);
  }

  switch (target_format)
  {
  case type_m32:
    convert_and_sort_morton<T, INDEX_T, uint32_t, 1>(tree_config, attributes_configs, public_header, points, smallest_scale, target_format, error);
    break;
  case type_m64:
    convert_and_sort_morton<T, INDEX_T, uint64_t, 1>(tree_config, attributes_configs, public_header, points, smallest_scale, target_format, error);
    break;
  case type_m128:
    convert_and_sort_morton<T, INDEX_T, uint64_t, 2>(tree_config, attributes_configs, public_header, points, smallest_scale, target_format, error);
    break;
  case type_m192:
    convert_and_sort_morton<T, INDEX_T, uint64_t, 3>(tree_config, attributes_configs, public_header, points, smallest_scale, target_format, error);
    break;
  default:
    assert(false);
    break;
  }
}

template <typename T>
void convert_and_sort_resolve_index_t(const tree_config_t &tree_config, attributes_configs_t &attributes_configs, const header_t &public_header, points_t &points, error_t &error)
{
  if (points.header.point_count < std::numeric_limits<uint16_t>::max())
  {
    convert_and_sort<T, uint16_t>(tree_config, attributes_configs, public_header, points, error);
  }
  else if (points.header.point_count < std::numeric_limits<uint32_t>::max())
  {
    convert_and_sort<T, uint32_t>(tree_config, attributes_configs, public_header, points, error);
  }
  else
  {
    convert_and_sort<T, uint64_t>(tree_config, attributes_configs, public_header, points, error);
  }
}

void sort_points(const tree_config_t &tree_config, attributes_configs_t &attributes_configs, const header_t &public_header, points_t &points, error_t &error)
{
  auto point_format = attributes_configs.get_point_format(points.attributes_id);
  switch (point_format.type)
  {
  case type_i32:
    convert_and_sort_resolve_index_t<int32_t>(tree_config, attributes_configs, public_header, points, error);
    break;
  default:
    assert(false);
  }
}
} // namespace points::converter
