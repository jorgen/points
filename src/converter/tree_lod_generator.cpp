/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#include "tree_lod_generator.hpp"

#include "worker.hpp"
#include "cache_file_handler.hpp"
#include "attributes_configs.hpp"
#include "morton_tree_coordinate_transform.hpp"
#include "input_header.hpp"
#include "morton.hpp"

#include <fmt/printf.h>

namespace points
{
namespace converter
{

const void *buffer_end(const buffer_t &buffer)
{
  return ((const uint8_t*)buffer.data) + buffer.size;
}
struct children_subset_t
{
  std::vector<points_subset_t> data;
  std::vector<int> data_skips;
  std::vector<int> skips;
  std::vector<int> lods;
  std::vector<tree_id_t> tree_id;
};

static input_data_id_t get_next_input_id(tree_cache_t &tree_cache)
{
  input_data_id_t ret;
  static_assert(sizeof(ret) == sizeof(tree_cache.current_lod_node_id), "input_data_id_t is incompatible with tree_cache_t::current_lod_node_id");
  memcpy(&ret, &tree_cache.current_lod_node_id, sizeof(ret));
  tree_cache.current_lod_node_id++;
  return ret;
}

std::pair<int,int> find_missing_lod(tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t tree_id,  const morton::morton192_t &min, const morton::morton192_t &max, const morton::morton192_t &parent_min, const morton::morton192_t &parent_max, int current_level, int skip, children_subset_t &to_lod)
{
  auto tree = tree_cache.get(tree_id);
  assert(skip < int(tree->nodes[current_level].size()));
  auto &node = tree->nodes[current_level][skip];
  assert(node || (tree->data[current_level][skip].point_count > 0 && tree->data[current_level][skip].point_count < uint64_t(-1)));
  int lod = morton::morton_tree_level_to_lod(tree->magnitude, current_level);
  if (!node)
  {
    const auto &data = tree->data[current_level][skip];
    assert(data.data.size());
    to_lod.data.insert(to_lod.data.end(), data.data.cbegin(), data.data.cend());
    int to_ret = int(data.data.size());
    to_lod.data_skips.push_back(to_ret);
    to_lod.skips.push_back(1);
    to_lod.lods.push_back(lod);
    return std::make_pair(1, to_ret);
  }

  int skip_index = 0;
  auto ret_pair = std::make_pair(0,0);
  if (min <= parent_min && parent_max <= max)
  {
    auto &node_data = tree->data[current_level][skip];
    assert(node_data.data.size() <= 1);
    if (node_data.data.size() == 1)
    {
      assert(node_data.data.back().offset.data == (~uint64_t(0)));
      to_lod.data.emplace_back(node_data.data.back());
      to_lod.data_skips.emplace_back(1);
      to_lod.skips.emplace_back(1);
      to_lod.lods.push_back(lod);
      return std::make_pair(1,1);
    }
    skip_index = int(to_lod.skips.size());
    node_data.data.emplace_back(get_next_input_id(tree_cache), offset_t(~uint64_t(0)), point_count_t(0));
    node_data.min = parent_min;
    node_data.max = parent_max;
    to_lod.data.emplace_back(node_data.data.back());
    to_lod.data_skips.emplace_back(1);
    to_lod.skips.emplace_back(1);
    to_lod.lods.push_back(lod);
    ret_pair = std::make_pair(1,1);
  }
  int child_count = 0;
  int sub_skip_parent = tree->skips[current_level][skip];
  for (int i = 0; i < 8; i++)
  {
    const bool has_this_child = node & (1 << i);
    if (has_this_child)
    {
      child_count++;
      morton::morton192_t child_min = parent_min;
      morton::morton_set_child_mask(lod, uint8_t(i), child_min);
      if (max < child_min)
        break;
      morton::morton192_t child_max = parent_max;
      morton::morton_set_child_mask(lod, uint8_t(i), child_max);
      if (child_max < min)
        continue;
      int sub_skip = sub_skip_parent + child_count - 1;
      std::pair<int, int> adjust = {};
      if (current_level == 4)
      {
        assert(sub_skip < int(tree->sub_trees.size()));
        tree_t *sub_tree = tree_cache.get(tree->sub_trees[sub_skip]);
        adjust = find_missing_lod(tree_cache, cache, sub_tree->id, min, max, child_min, child_max, 0, 0, to_lod);
      }
      else
      {
        adjust = find_missing_lod(tree_cache, cache, tree_id, min, max, child_min, child_max, current_level + 1, sub_skip, to_lod);
      }
      ret_pair.first += adjust.first;
      ret_pair.second += adjust.second;
      if (ret_pair.first > 0)
      {
        to_lod.skips[skip_index] += adjust.first;
        to_lod.data_skips[skip_index] += adjust.second;
      }
    }
  }
  return ret_pair;
}

static lod_tree_worker_data_t make_tree_worker_data(const tree_t &tree)
{
  lod_tree_worker_data_t ret;
  ret.tree_id = tree.id;
  ret.magnitude = tree.magnitude;
  for (int i = 0; i < 5; i++)
  {
    ret.nodes[i].reserve(tree.skips[i].size());
  }
  return ret;
}

struct tree_iterator_t
{
  tree_iterator_t(size_t capasity)
  {
    parent_indecies.reserve(capasity);
    skips.reserve(capasity);
    names.reserve(capasity);
    parents.reserve(capasity / 2);
  }

  void clear()
  {
    parent_indecies.clear();
    skips.clear();
    names.clear();
    parents.clear();
  }
  std::vector<uint16_t> parent_indecies;
  std::vector<uint16_t> skips;
  std::vector<uint16_t> names;
  std::vector<lod_node_worker_data_t> parents;
};

static void tree_get_work_items(tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t &tree_id, const morton::morton192_t &min, const morton::morton192_t &max, lod_node_worker_data_t &parent_node, std::vector<lod_tree_worker_data_t> &to_lod)
{
  auto tree = tree_cache.get(tree_id);
  auto lod_tree_worker_data = make_tree_worker_data(*tree);

  if (tree->nodes->empty())
    return;

  bool buffer_index = false;

  size_t capasity = std::max(tree->sub_trees.size(), std::max(std::max(tree->skips[4].size(), tree->skips[3].size()), tree->skips[2].size()));
  tree_iterator_t tree_iterator[2] = { capasity, capasity };

  auto *parent_buffer = &parent_node;
  tree_iterator[buffer_index].parent_indecies.emplace_back(0);
  tree_iterator[buffer_index].skips.emplace_back(0);
  uint16_t root_name = morton::morton_get_child_mask(morton::morton_magnitude_to_lod(tree->magnitude), tree->morton_min);
  tree_iterator[buffer_index].names.emplace_back(root_name << 4);

  for (int level = 0; level < 5 && tree_iterator[buffer_index].parent_indecies.size(); level++)
  {
    for (int to_process_index = 0; to_process_index < int(tree_iterator[buffer_index].skips.size()); to_process_index++)
    {
      auto tree_skip = tree_iterator[buffer_index].skips[to_process_index];
      auto node = tree->nodes[level][tree_skip];
      auto &parent = parent_buffer[tree_iterator[buffer_index].parent_indecies[to_process_index]];
      auto &data = tree->data[level][tree_skip];
      if (node)
      {
        assert(data.data.size() <= 1);
        if (data.data.empty())
        {
          data.data.emplace_back(get_next_input_id(tree_cache), offset_t(~uint64_t(0)), point_count_t(0));
        }
        uint16_t parent_index = uint16_t(tree_iterator[!buffer_index].parents.size());
        tree_iterator[!buffer_index].parents.emplace_back();
        auto &this_node = tree_iterator[!buffer_index].parents.back();
        this_node.id = tree->node_ids[level][tree_skip];
        this_node.lod = uint16_t(morton::morton_magnitude_to_lod(tree->magnitude) + level);
        this_node.storage_name = data.data.front().input_id;
        this_node.generated_point_count.data = 0;
        int child_count = 0;
        uint16_t parent_node_name = level == 4 ? 0 : this_node.id;
        int node_name_level = level == 4 ? 0 : level + 1;
        for (int child_index = 0; child_index < 8; child_index++)
        {
          if (!(node & uint8_t(1 << child_index)))
            continue;
          auto &sub_level = tree_iterator[!buffer_index];
          sub_level.names.push_back(morton::morton_get_name(parent_node_name, node_name_level, child_index));
          sub_level.skips.push_back(tree->skips[level][tree_skip] + child_count);
          sub_level.parent_indecies.push_back(parent_index);
          child_count++;
        }
      }
      parent.child_data.insert(parent.child_data.end(), data.data.begin(), data.data.end());
    }

    if (level > 0)
    {
      lod_tree_worker_data.nodes[level - 1] = std::move(tree_iterator[buffer_index].parents);
    }
    tree_iterator[buffer_index].clear();
    buffer_index = !buffer_index;
    parent_buffer = tree_iterator[buffer_index].parents.data();
  }
  //sub-trees
  for (int to_process_index = 0; to_process_index < int(tree_iterator[buffer_index].skips.size()); to_process_index++)
  {
    auto tree_skip = tree_iterator[buffer_index].skips[to_process_index];
    auto subtree = tree->sub_trees[tree_skip];
    auto &parent = parent_buffer[tree_iterator[buffer_index].parent_indecies[to_process_index]];
    tree_get_work_items(tree_cache, cache, subtree, min, max, parent, to_lod);
  }
  lod_tree_worker_data.nodes[4] = std::move(tree_iterator[buffer_index].parents);
  if (lod_tree_worker_data.nodes[0].size())
    to_lod.push_back(std::move(lod_tree_worker_data));
}


lod_worker_t::lod_worker_t(tree_lod_generator_t &lod_generator, cache_file_handler_t &cache, attributes_configs_t &attributes_configs, lod_node_worker_data_t &data, int &inc_on_completed)
  : lod_generator(lod_generator)
  , cache(cache)
  , attributes_configs(attributes_configs)
  , data(data)
  , inc_on_completed(inc_on_completed)
{
}

template<typename T, size_t S>
struct structured_data_t
{
  T data[S];
};

template<size_t S_S, typename S, size_t D_S, typename D>
uint32_t convert_points_impl(uint32_t step, const std::pair<type_t, components_t> &source_format, const buffer_t &source, const std::pair<type_t, components_t> &destination_format, buffer_t &destination)
{
  (void)source_format;
  (void)destination_format;
  using source_data_type_t = structured_data_t<S, S_S>;
  using destination_data_type_t = structured_data_t<D, D_S>;
  auto *source_it = reinterpret_cast<const source_data_type_t *>(source.data);
  auto *destination_it = reinterpret_cast<destination_data_type_t *>(destination.data);
  assert(source.size % sizeof(source_data_type_t) == 0);
  assert(destination.size % sizeof(destination_data_type_t) == 0);
  auto *source_end = source_it+ (source.size / sizeof(source_data_type_t));
  auto *destination_end = destination_it + (destination.size / sizeof(destination_data_type_t));

  auto copy_components = std::min(S_S, D_S);
  uint32_t converted = 0;
  for (;source_it < source_end && destination_it < destination_end; source_it += step, destination_it++, converted++)
  {
    for (int i = 0; i < int(copy_components); i++)
    {
      destination_it->data[i] = D(source_it->data[i]);
    }
    for (int i = int(copy_components); i < int(D_S); i++)
    {
      destination_it->data[i] = D();
    }
  }
  return converted;
}

template<size_t S_S, typename S, size_t D_S>
uint32_t convert_points_three(uint32_t step, const std::pair<type_t, components_t> &source_format, const buffer_t &source, const std::pair<type_t, components_t> &destination_format, buffer_t &destination)
{
  switch(destination_format.first)
  {
  case type_u8: return convert_points_impl<S_S, S, D_S, uint8_t>(step, source_format, source, destination_format, destination);
  case type_i8: return convert_points_impl<S_S, S, D_S, int8_t>(step, source_format, source, destination_format, destination);
  case type_u16: return convert_points_impl<S_S,S, D_S,  uint16_t>(step, source_format, source, destination_format, destination);
  case type_i16: return convert_points_impl<S_S,S, D_S,  int16_t>(step, source_format, source, destination_format, destination);
  case type_u32: return convert_points_impl<S_S,S, D_S,  uint32_t>(step, source_format, source, destination_format, destination);
  case type_i32: return convert_points_impl<S_S,S, D_S,  int32_t>(step, source_format, source, destination_format, destination);
  case type_r32: return convert_points_impl<S_S,S, D_S,  float>(step, source_format, source, destination_format, destination);
  case type_u64: return convert_points_impl<S_S,S, D_S,  uint64_t>(step, source_format, source, destination_format, destination);
  case type_i64: return convert_points_impl<S_S,S, D_S,  int64_t>(step, source_format, source, destination_format, destination);
  case type_r64: return convert_points_impl<S_S,S, D_S,  double>(step, source_format, source, destination_format, destination);
  case type_m32: return convert_points_impl<S_S, S, 1, uint32_t>(step, source_format, source, destination_format, destination);
  case type_m64: return convert_points_impl<S_S, S, 1, uint64_t>(step, source_format, source, destination_format, destination);
  case type_m128: return convert_points_impl<S_S, S, 2, uint64_t>(step, source_format, source, destination_format, destination);
  case type_m192: return convert_points_impl<S_S, S, 3, uint64_t>(step, source_format, source, destination_format, destination);
  }
  return 0;
}

template<size_t S_S, typename S>
uint32_t convert_points_two(uint32_t step, const std::pair<type_t, components_t> &source_format, const buffer_t &source, const std::pair<type_t, components_t> &destination_format, buffer_t &destination)
{
  switch(destination_format.second)
  {
  case components_1: return convert_points_three<S_S, S, 1>(step, source_format, source, destination_format, destination);
  case components_2: return convert_points_three<S_S, S, 2>(step, source_format, source, destination_format, destination);
  case components_3: return convert_points_three<S_S, S, 3>(step, source_format, source, destination_format, destination);
  case components_4: return convert_points_three<S_S, S, 4>(step, source_format, source, destination_format, destination);
  case components_4x4: return convert_points_three<S_S, S, 4*4>(step, source_format, source, destination_format, destination);
  }
  return 0;
}
template<size_t S_S>
uint32_t convert_points_one(uint32_t step, const std::pair<type_t, components_t> &source_format, const buffer_t &source, const std::pair<type_t, components_t> &destination_format, buffer_t &destination)
{
  switch(source_format.first)
  {
  case type_u8: return convert_points_two<S_S, uint8_t>(step, source_format, source, destination_format, destination);
  case type_i8: return convert_points_two<S_S, int8_t>(step, source_format, source, destination_format, destination);
  case type_u16: return convert_points_two<S_S, uint16_t>(step, source_format, source, destination_format, destination);
  case type_i16: return convert_points_two<S_S, int16_t>(step, source_format, source, destination_format, destination);
  case type_u32: return convert_points_two<S_S, uint32_t>(step, source_format, source, destination_format, destination);
  case type_i32: return convert_points_two<S_S, int32_t>(step, source_format, source, destination_format, destination);
  case type_r32: return convert_points_two<S_S, float>(step, source_format, source, destination_format, destination);
  case type_u64: return convert_points_two<S_S, uint64_t>(step, source_format, source, destination_format, destination);
  case type_i64: return convert_points_two<S_S, int64_t>(step, source_format, source, destination_format, destination);
  case type_r64: return convert_points_two<S_S, double>(step, source_format, source, destination_format, destination);
  case type_m32: return convert_points_two<1, uint32_t>(step, source_format, source, destination_format, destination);
  case type_m64: return convert_points_two<1, uint64_t>(step, source_format, source, destination_format, destination);
  case type_m128: return convert_points_two<2, uint64_t>(step, source_format, source, destination_format, destination);
  case type_m192: return convert_points_two<3, uint64_t>(step, source_format, source, destination_format, destination);
  }
  return 0;
}

uint32_t quantize_points(uint32_t step, const std::pair<type_t, components_t> &source_format, const buffer_t &source, const std::pair<type_t, components_t> &destination_format, buffer_t &destination)
{
  switch(source_format.second)
  {
  case components_1: return convert_points_one<1>(step, source_format, source, destination_format, destination);
  case components_2: return convert_points_one<2>(step, source_format, source, destination_format, destination);
  case components_3: return convert_points_one<3>(step, source_format, source, destination_format, destination);
  case components_4: return convert_points_one<4>(step, source_format, source, destination_format, destination);
  case components_4x4: return convert_points_one<4*4>(step, source_format, source, destination_format, destination);
  }
  return 0;
}

template<typename S_M, typename D_M>
static typename std::enable_if<sizeof(S_M) == sizeof(D_M), void>::type copy_morton(const S_M &s, const morton::morton192_t &morton_min, D_M &d)
{
  (void)morton_min;
  memcpy(&d, &s, sizeof(d));
}

template<size_t A, size_t B>
struct less_than
{
  enum the_value { value = A < B };
};
template<size_t A, size_t B>
struct greater_than
{
  enum the_value { value = A > B };
};

template<typename S_M, typename D_M>
static typename std::enable_if<less_than<sizeof(S_M), sizeof(D_M)>::value, void>::type copy_morton(const S_M &s, const morton::morton192_t &morton_min, D_M &d)
{
  morton::morton_upcast(s, morton_min, d);
}

template<typename S_M, typename D_M>
static typename std::enable_if<greater_than<sizeof(S_M), sizeof(D_M)>::value, void>::type copy_morton(const S_M &s, const morton::morton192_t &morton_min, D_M &d)
{
  (void)s;
  (void)morton_min;
  (void)d;
  morton::morton_downcast(s, d);
}

template<typename S_M, typename D_M>
static uint32_t quantize_morton_two(uint32_t step, const morton::morton192_t &morton_min, type_t source_type, const buffer_t &source, type_t destination_type, buffer_t &destination)
{
  (void)source_type;
  (void)destination_type;
  auto *source_it = reinterpret_cast<const S_M *>(source.data);
  auto *destination_it = reinterpret_cast<D_M *>(destination.data);
  assert(source.size % sizeof(S_M) == 0);
  assert(destination.size % sizeof(D_M) == 0);
  auto *source_end = source_it+ (source.size / sizeof(S_M));
  auto *destination_end = destination_it + (destination.size / sizeof(D_M));
  uint32_t converted = 0;
  for (;source_it < source_end && destination_it < destination_end; source_it += step, destination_it++, converted++)
  {
    copy_morton<S_M, D_M>(*source_it, morton_min, *destination_it);
  }
  return converted;
}

template<typename S_M>
static uint32_t quantize_morton_one(uint32_t step, const morton::morton192_t &morton_min, type_t source_type, const buffer_t &source, type_t destination_type, buffer_t &destination)
{
  assert(destination_type == type_m32
         || destination_type == type_m64
         || destination_type == type_m128
         || destination_type == type_m192);
  switch(destination_type)
  {
  case type_m32: return quantize_morton_two<S_M, morton::morton32_t>(step, morton_min, source_type, source, destination_type, destination);
  case type_m64: return quantize_morton_two<S_M, morton::morton64_t>(step, morton_min, source_type, source, destination_type, destination);
  case type_m128: return quantize_morton_two<S_M, morton::morton128_t>(step, morton_min, source_type, source, destination_type, destination);
  case type_m192: return quantize_morton_two<S_M, morton::morton192_t>(step, morton_min, source_type, source, destination_type, destination);
  default:
    break;
  }

  return 0;
}

static uint32_t quantize_morton(uint32_t step, const morton::morton192_t &morton_min, type_t source_type, const buffer_t &source, type_t destination_type, buffer_t &destination)
{
  assert(source_type == type_m32
         || source_type == type_m64
         || source_type == type_m128
         || source_type == type_m192);

  switch(source_type)
  {
  case type_m32: return quantize_morton_one<morton::morton32_t>(step, morton_min, source_type, source, destination_type, destination);
  case type_m64: return quantize_morton_one<morton::morton64_t>(step, morton_min, source_type, source, destination_type, destination);
  case type_m128: return quantize_morton_one<morton::morton128_t>(step, morton_min, source_type, source, destination_type, destination);
  case type_m192: return quantize_morton_one<morton::morton192_t>(step, morton_min, source_type, source, destination_type, destination);
  default:
    break;
  }

  return 0;
}

static buffer_t morton_buffer_for_subset(const buffer_t &buffer, type_t format, offset_t offset, point_count_t count)
{
  uint64_t format_byte_size = uint64_t(size_for_format(format));
  buffer_t ret;
  auto offset_bytes = offset.data * format_byte_size;
  ret.data = ((uint8_t *)buffer.data) + offset_bytes;
  ret.size = uint64_t(count.data) * format_byte_size;
  assert(ret.size + offset.data <= buffer.size);
  return ret;
}

static buffer_t morton_buffer_for_target(const buffer_t &buffer, std::pair<type_t, components_t> format, offset_t offset)
{
  int format_byte_size = size_for_format(format.first);
  buffer_t ret;
  auto offset_bytes = offset.data * format_byte_size;
  ret.data = ((uint8_t *)buffer.data) + offset_bytes;
  ret.size = buffer.size - offset_bytes;
  return ret;
}

static buffer_t buffer_for_target(const buffer_t &buffer, std::pair<type_t, components_t> format, offset_t offset)
{
  int format_byte_size = size_for_format(format.first, format.second);
  buffer_t ret;
  auto offset_bytes = offset.data * format_byte_size;
  ret.data = ((uint8_t *)buffer.data) + offset_bytes;
  ret.size = buffer.size - offset_bytes;
  return ret;
}

static buffer_t buffer_for_subset(const buffer_t &buffer, std::pair<type_t, components_t> format, offset_t offset, point_count_t count)
{
  int format_byte_size = size_for_format(format.first, format.second);
  buffer_t ret;
  auto offset_bytes = offset.data * format_byte_size;
  ret.data = ((uint8_t *)buffer.data) + offset_bytes;
  ret.size = uint64_t(count.data) * format_byte_size;
  assert(ret.size + offset.data <= buffer.size);
  return ret;
}

static bool buffer_is_subset(const buffer_t &super, const buffer_t &sub)
{
  return super.data <= sub.data && buffer_end(sub) <= buffer_end(super);
}

static void update_destination_header(const header_t &source_header, header_t &destination_header)
{
  if (source_header.min[0] < destination_header.min[0])
    destination_header.min[0] = source_header.min[0];
  if (source_header.min[1] < destination_header.min[1])
    destination_header.min[1] = source_header.min[1];
  if (source_header.min[2] < destination_header.min[2])
    destination_header.min[2] = source_header.min[2];
  if (destination_header.max[0] < source_header.max[0])
    destination_header.max[0] = source_header.max[0];
  if (destination_header.max[1] < source_header.max[1])
    destination_header.max[1] = source_header.max[1];
  if (destination_header.max[2] < source_header.max[2])
    destination_header.max[2] = source_header.max[2];
}

static void update_destination_header(const storage_header_t &source_header, storage_header_t &destination_header)
{
  update_destination_header(source_header.public_header, destination_header.public_header);
  if (source_header.morton_min < destination_header.morton_min)
    destination_header.morton_min = source_header.morton_min;
  if (destination_header.morton_max < source_header.morton_max)
    destination_header.morton_max = source_header.morton_max;
}

uint32_t quantize_to_parent(const points_subset_t &child, uint32_t count, cache_file_handler_t &file_cache, const std::vector<std::pair<type_t, components_t>> &destination_map, const attribute_lod_info_t &source_maping, attribute_buffers_t &destination_buffers, offset_t destination_offset, storage_header_t &destination_header)
{
  auto &source_map = source_maping.source_attributes;
  assert(destination_map.size() == source_map.size()
         && destination_map.size() == destination_buffers.buffers.size());
  assert(count <= child.count.data);
  uint32_t step = child.count.data / count;
  uint32_t quantized_morton = 0;
  {
    read_points_t child_data(file_cache, child.input_id, source_map[0].source_index);
    update_destination_header(child_data.header, destination_header);
    const buffer_t source_buffer = morton_buffer_for_subset(child_data.data,child_data.header.point_format.first, child.offset, child.count);
    assert(buffer_is_subset(child_data.data, source_buffer));
    buffer_t  destination_buffer = morton_buffer_for_target(destination_buffers.buffers[0], destination_map[0], destination_offset);
    assert(buffer_is_subset(destination_buffers.buffers[0], destination_buffer));
    quantized_morton = quantize_morton(step, child_data.header.morton_min, child_data.header.point_format.first, source_buffer, destination_map[0].first, destination_buffer);
  }
  for (int i = 1; i < int(destination_map.size()); i++)
  {
    if (source_map[i].source_index > 0)
    {
    read_points_t child_data(file_cache, child.input_id, source_map[i].source_index);
    const buffer_t source_buffer = buffer_for_subset(child_data.data,source_map[i].format, child.offset, child.count);
    assert(buffer_is_subset(child_data.data, source_buffer));
    buffer_t  destination_buffer = buffer_for_target(destination_buffers.buffers[i], destination_map[i], destination_offset);
    assert(buffer_is_subset(destination_buffers.buffers[i], destination_buffer));
    auto quantized_attribute = quantize_points(step, source_map[i].format, source_buffer, destination_map[i], destination_buffer);
    assert(quantized_attribute == quantized_morton);
    }
    else
    {
      assert(false && "insert no values (0)");
    }
  }
  return quantized_morton;
}

void lod_worker_t::work()
{
  uint64_t total_count = 0;
  attributes_t attributes;
  std::unique_ptr<attributes_id_t[]> attribute_ids(new attributes_id_t[data.child_data.size()]);
  for (int i = 0; i < int(data.child_data.size()); i++)
  {
    auto &child = data.child_data[i];
    //child.
    point_count_t count(0);
    bool got_attrib = cache.attribute_id_and_count_for_input_id(child.input_id, attribute_ids[i], count);
    if (child.count.data == 0) //adjust for lod data
    {
      child.count.data = count.data;
      child.offset.data = 0;
    }

    total_count += child.count.data;
    (void) got_attrib;
  }

  auto lod_format = morton_format_from_lod(data.lod);

  auto lod_attrib_mapping = attributes_configs.get_lod_attribute_mapping(data.lod, attribute_ids.get(), attribute_ids.get() + data.child_data.size());

  storage_header_t destination_header;
  storage_header_initialize(destination_header);
  destination_header.input_id = data.storage_name;
  attribute_buffers_t buffers;
  attribute_buffers_initialize(lod_attrib_mapping.destination, buffers, total_count);

  offset_t total_acc_count(0);
  for (int i = 0; i < int(data.child_data.size()); i++)
  {
    auto &child = data.child_data[i];
    double ratio = std::min(double(lod_generator.global_state().node_limit - total_acc_count.data) / double(total_count), 1.0);
    uint32_t child_count = std::min(std::min(uint32_t(std::round(child.count.data * ratio)), child.count.data),
                                    uint32_t(total_count - total_acc_count.data));

    if (child_count > 0)
    {
      auto &source_mapping = lod_attrib_mapping.get_source_mapping(attribute_ids.get()[i]);
      total_acc_count.data += quantize_to_parent(child, child_count, cache, lod_attrib_mapping.destination, source_mapping, buffers, total_acc_count, destination_header);
      (void) attributes_configs;
    }
    else
    {
      auto &source_mapping = lod_attrib_mapping.get_source_mapping(attribute_ids.get()[i]);
      read_points_t child_data(cache, child.input_id, source_mapping.source_attributes[0].source_index);
      update_destination_header(child_data.header, destination_header);
    }
  }
  attribute_buffers_adjust_buffers_to_size(lod_attrib_mapping.destination, buffers, total_acc_count.data);
  destination_header.public_header.point_count = total_acc_count.data;
  destination_header.point_format = {lod_format, components_1};
  destination_header.lod_span = data.lod;
  cache.write(destination_header, std::move(buffers), lod_attrib_mapping.destination_id);
  data.generated_point_count.data = total_acc_count.data;
}

void lod_worker_t::after_work(completion_t completion)
{
  (void)completion;
  inc_on_completed++;
  lod_generator.iterate_workers();
}

static void iterate_batch(tree_lod_generator_t &lod_generator, lod_worker_batch_t &batch, cache_file_handler_t &cache_file, attributes_configs_t &attributes_configs, threaded_event_loop_t &loop)
{
  batch.new_batch = false;
  batch.lod_workers.clear();
  batch.level--;
  batch.completed = 0;

  size_t batch_size = 0;
  while (batch_size == 0 && batch.level > 0)
  {
    for (auto &tree : batch.worker_data)
      batch_size += tree.nodes[batch.level].size();
    if (batch_size == 0)
      batch.level--;
  }

  batch.lod_workers.reserve(batch_size);

  for (auto &tree : batch.worker_data)
  {
    for (auto &node : tree.nodes[batch.level])
    {
      assert(node.child_data.size());
      batch.lod_workers.emplace_back(lod_generator, cache_file, attributes_configs, node, batch.completed);
      batch.lod_workers.back().enqueue(loop);
    }
  }
}

tree_lod_generator_t::tree_lod_generator_t(threaded_event_loop_t &loop, const tree_global_state_t &tree_global_state, tree_cache_t &tree_cache, cache_file_handler_t &file_cache, attributes_configs_t &attributes_configs)
  : _loop(loop)
  , _tree_global_state(tree_global_state)
  , _tree_cache(tree_cache)
  , _file_cache(file_cache)
  , _attributes_configs(attributes_configs)
{
  (void)_tree_cache;
  (void)_generated_until;
}

void tree_lod_generator_t::generate_lods(tree_id_t &tree_id, const morton::morton192_t &max)
{
  (void)tree_id;
  (void)max;
  //auto &worker_data = batch.worker_data;
  std::vector<lod_tree_worker_data_t> to_lod;
  lod_node_worker_data_t fake_parent;
  tree_get_work_items(_tree_cache, _file_cache, tree_id, _generated_until, max, fake_parent, to_lod);
  if (to_lod.size())
  {
    std::sort(to_lod.begin(), to_lod.end(), [](const lod_tree_worker_data_t &a, const lod_tree_worker_data_t &b) { return a.magnitude < b.magnitude; });
    int current_level = to_lod.front().magnitude;
    int batch_start = 0;
    for (int i = 0; i < int(to_lod.size()); i++)
    {
      auto &current = to_lod[i];
      if (current.magnitude != current_level)
      {
        _lod_batches.emplace_back(new lod_worker_batch_t());
        auto &batch = *_lod_batches.back();
        batch.worker_data.insert(batch.worker_data.end(), std::make_move_iterator(to_lod.begin() + batch_start), std::make_move_iterator(to_lod.begin() + i));
        current_level = current.magnitude;
        batch_start = i;
      }
    }
    _lod_batches.emplace_back(new lod_worker_batch_t());
    auto &batch = *_lod_batches.back();
    batch.worker_data.insert(batch.worker_data.end(), std::make_move_iterator(to_lod.begin() + batch_start), std::make_move_iterator(to_lod.end()));
  }
  iterate_workers();
}

static void adjust_tree_after_lod(tree_cache_t &tree_cache, cache_file_handler_t &cache, const std::vector<lod_tree_worker_data_t> &to_adjust)
{
  for (auto &adjust_data : to_adjust)
  {
    tree_t *tree = tree_cache.get(adjust_data.tree_id);
    for (int level = 0; level < 5; level++)
    {
      if (adjust_data.nodes[level].empty())
        break;
      int tree_index = 0;
      for (int node_index = 0; node_index < int(adjust_data.nodes[level].size()); node_index++)
      {
        auto current = adjust_data.nodes[level][node_index].id;
        auto &node_ids = tree->node_ids[level];

        while (tree_index < int(node_ids.size()) && node_ids[tree_index] < current)
          tree_index++;
        assert(node_ids[tree_index] == current);
        tree->data[level][tree_index].point_count = adjust_data.nodes[level][node_index].generated_point_count.data;
      }
    }
  }
}

void tree_lod_generator_t::iterate_workers()
{
  if (_lod_batches.size() && _lod_batches.front()->completed == int(_lod_batches.front()->lod_workers.size()) && _lod_batches.front()->level == 0)
  {
    adjust_tree_after_lod(_tree_cache, _file_cache, _lod_batches.front()->worker_data);
    _lod_batches.pop_front();
  }
  if (_lod_batches.size() && (_lod_batches.front()->new_batch || _lod_batches.front()->completed == int(_lod_batches.front()->lod_workers.size())))
    iterate_batch(*this, *_lod_batches.front(), _file_cache, _attributes_configs, _loop);
  if (_lod_batches.empty())
  {
    fmt::print(stderr, "Done\n");
  }
}

}}//namespace
