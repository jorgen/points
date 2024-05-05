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

#include "attributes_configs.hpp"
#include "cache_file_handler.hpp"
#include "input_header.hpp"
#include "morton.hpp"
#include "morton_tree_coordinate_transform.hpp"
#include "worker.hpp"

#include <fixed_size_vector.hpp>
#include <fmt/printf.h>
#include <numeric>
#include <random>

namespace points::converter
{

static const void *buffer_end(const buffer_t &buffer)
{
  return ((const uint8_t *)buffer.data) + buffer.size;
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
  input_data_id_t ret; // NOLINT(*-pro-type-member-init)
  static_assert(sizeof(ret) == sizeof(tree_cache.current_lod_node_id), "input_data_id_t is incompatible with tree_cache_t::current_lod_node_id");
  memcpy(&ret, &tree_cache.current_lod_node_id, sizeof(ret));
  tree_cache.current_lod_node_id++;
  return ret;
}

std::pair<int, int> find_missing_lod(tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t tree_id, const morton::morton192_t &min, const morton::morton192_t &max, const morton::morton192_t &parent_min,
                                     const morton::morton192_t &parent_max, int current_level, int skip, children_subset_t &to_lod) // NOLINT(*-no-recursion)
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
  auto ret_pair = std::make_pair(0, 0);
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
      return std::make_pair(1, 1);
    }
    skip_index = int(to_lod.skips.size());
    node_data.data.emplace_back(get_next_input_id(tree_cache), offset_in_subset_t(~uint32_t(0)), point_count_t(0));
    node_data.min = parent_min;
    node_data.max = parent_max;
    to_lod.data.emplace_back(node_data.data.back());
    to_lod.data_skips.emplace_back(1);
    to_lod.skips.emplace_back(1);
    to_lod.lods.push_back(lod);
    ret_pair = std::make_pair(1, 1);
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

static void tree_get_work_items(tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t &tree_id, lod_node_worker_data_t &parent_node, std::vector<lod_tree_worker_data_t> &to_lod)
{
  auto tree = tree_cache.get(tree_id);
  assert(!(tree->morton_min < parent_node.node_min));
  auto lod_tree_worker_data = make_tree_worker_data(*tree);

  if (tree->nodes->empty())
    return;

  bool buffer_index = false;

  size_t capasity = std::max(tree->sub_trees.size(), std::max(std::max(tree->skips[4].size(), tree->skips[3].size()), tree->skips[2].size()));
  tree_iterator_t tree_iterator[2] = {capasity, capasity};

  auto *parent_buffer = &parent_node;
  tree_iterator[buffer_index].parent_indecies.emplace_back(0);
  tree_iterator[buffer_index].skips.emplace_back(0);
  uint16_t root_name = morton::morton_get_child_mask(morton::morton_magnitude_to_lod(tree->magnitude), tree->morton_min);
  tree_iterator[buffer_index].names.emplace_back(root_name << 4 * 3);

  for (int level = 0; level < 5 && tree_iterator[buffer_index].parent_indecies.size(); level++)
  {
    for (int to_process_index = 0; to_process_index < int(tree_iterator[buffer_index].skips.size()); to_process_index++)
    {
      auto tree_skip = tree_iterator[buffer_index].skips[to_process_index];
      auto node = tree->nodes[level][tree_skip];
      auto &parent = parent_buffer[tree_iterator[buffer_index].parent_indecies[to_process_index]];
      auto &data = tree->data[level][tree_skip];
      auto name = tree_iterator[buffer_index].names[to_process_index];
      morton::morton192_t node_min = morton::set_name_in_morton(tree->magnitude, tree->morton_min, name);
      assert(!(node_min < parent.node_min));
#ifndef NDEBUG
      auto min_from_mins = tree->mins[level][tree_skip];
      auto max_from_mins = morton::create_max(morton::morton_tree_level_to_lod(tree->magnitude, level), min_from_mins);
      assert(min_from_mins == node_min);
      morton::morton192_t parent_max = morton::create_max(morton::morton_tree_level_to_lod(tree->magnitude + (level == 0), level == 0 ? 4 : level - 1), node_min);
      morton::morton192_t node_max = morton::create_max(morton::morton_tree_level_to_lod(tree->magnitude, level), node_min);
      assert(max_from_mins == node_max);
      assert(!(parent_max < node_max));
#endif
      if (node)
      {
        assert(data.data.size() <= 1);
        if (data.data.empty())
        {
          data.data.emplace_back(get_next_input_id(tree_cache), offset_in_subset_t(~uint32_t(0)), point_count_t(0));
        }
        uint16_t parent_index = uint16_t(tree_iterator[!buffer_index].parents.size());
        tree_iterator[!buffer_index].parents.emplace_back();
        auto &this_node = tree_iterator[!buffer_index].parents.back();
        this_node.id = tree->node_ids[level][tree_skip];
        this_node.lod = uint16_t(morton::morton_tree_level_to_lod(tree->magnitude, level));
        this_node.node_min = node_min;
        this_node.storage_name = data.data.front().input_id;
        this_node.generated_point_count.data = 0;
        int child_count = 0;
        uint16_t parent_node_name = level == 4 ? 0 : this_node.id << 3;
        int node_name_level = level == 4 ? 0 : level;
        for (int child_index = 0; child_index < 8; child_index++)
        {
          if (!(node & uint8_t(1 << child_index)))
            continue;
          auto &sub_level = tree_iterator[!buffer_index];
          sub_level.names.push_back(morton::morton_get_name(parent_node_name, node_name_level, child_index));
          sub_level.skips.push_back(uint16_t(tree->skips[level][tree_skip] + child_count));
          sub_level.parent_indecies.push_back(parent_index);
          child_count++;
        }
      }
      if (data.data.size())
      {
        parent.child_data.push_back(data);
        parent.child_trees.push_back(tree_id);
      }
    }

    if (level > 0)
    {
      lod_tree_worker_data.nodes[level - 1] = std::move(tree_iterator[buffer_index].parents);
    }
    tree_iterator[buffer_index].clear();
    buffer_index = !buffer_index;
    parent_buffer = tree_iterator[buffer_index].parents.data();
  }
  // sub-trees
  for (int to_process_index = 0; to_process_index < int(tree_iterator[buffer_index].skips.size()); to_process_index++)
  {
    auto tree_skip = tree_iterator[buffer_index].skips[to_process_index];
    auto subtree_id = tree->sub_trees[tree_skip];
    auto &parent = parent_buffer[tree_iterator[buffer_index].parent_indecies[to_process_index]];
    tree_get_work_items(tree_cache, cache, subtree_id, parent, to_lod);
  }
  lod_tree_worker_data.nodes[4] = std::move(tree_iterator[buffer_index].parents);
  if (lod_tree_worker_data.nodes[0].size())
    to_lod.push_back(std::move(lod_tree_worker_data));
}

lod_worker_t::lod_worker_t(tree_lod_generator_t &lod_generator, lod_worker_batch_t &batch, cache_file_handler_t &cache, attributes_configs_t &attributes_configs, lod_node_worker_data_t &data, const std::vector<float> &random_offsets)
  : lod_generator(lod_generator)
  , batch(batch)
  , cache(cache)
  , attributes_configs(attributes_configs)
  , data(data)
  , random_offsets(random_offsets)
{
}

template <typename S_M, typename D_M>
static typename std::enable_if<sizeof(S_M) == sizeof(D_M), void>::type copy_morton(const S_M &s, const morton::morton192_t &morton_min, const morton::morton192_t &morton_max, D_M &d)
{
  (void)morton_min;
  memcpy(&d, &s, sizeof(d));
#ifndef NDEBUG
  S_M morton_min_downcasted;
  morton::morton_downcast(morton_min, morton_min_downcasted);
  assert(!(s < morton_min_downcasted));
  S_M morton_max_downcasted;
  morton::morton_downcast(morton_max, morton_max_downcasted);
  assert(!(morton_max_downcasted < s));
#endif
}

template <size_t A, size_t B>
struct less_than
{
  enum the_value
  {
    value = A < B
  };
};
template <size_t A, size_t B>
struct greater_than
{
  enum the_value
  {
    value = A > B
  };
};

template <typename S_M, typename D_M>
static typename std::enable_if<less_than<sizeof(S_M), sizeof(D_M)>::value, void>::type copy_morton(const S_M &s, const morton::morton192_t &morton_min, const morton::morton192_t &morton_max, D_M &d)
{
  (void)morton_min;
  (void)morton_max;
  morton::morton_upcast(s, morton_min, d);
}

template <typename S_M, typename D_M>
static typename std::enable_if<greater_than<sizeof(S_M), sizeof(D_M)>::value, void>::type copy_morton(const S_M &s, const morton::morton192_t &morton_min, const morton::morton192_t &morton_max, D_M &d)
{
  (void)morton_min;
  (void)morton_max;
  morton::morton_downcast(s, d);
#ifndef NDEBUG
  S_M upcasted;
  morton::morton_upcast(d, morton_min, upcasted);
  assert(upcasted == s);

  for (int i = int(std::size(d.data)); i < int(std::size(s.data)); i++)
  {
    assert(s.data[i] == morton_min.data[i]);
  }
#endif
}

template <typename T, size_t N>
struct morton_to_lod_t
{
  morton::morton_t<T, N> morton;
  offset_in_subset_t index;
  input_data_id_t id;
};

struct storage_location_range_t
{
  const std::vector<storage_location_t> &location;
  attributes_id_t attributes_id;
  uint32_t end;
};

template <typename S_M, typename T, size_t N>
static void find_indecies_to_quantize(input_data_id_t input_id, const morton::morton192_t &min, const buffer_t &source, offset_in_subset_t offset, point_count_t point_count, int maskWidth,
                                      const std::vector<float> &random_offsets, std::vector<morton_to_lod_t<T, N>> &morton_to_lod)
{
  auto *source_it = reinterpret_cast<const S_M *>(source.data);
  assert(source.size % sizeof(S_M) == 0);
  assert(source_it + point_count.data == source_it + (source.size / sizeof(S_M)));
  uint32_t range_start = 0;
  S_M currentMaxVal = morton::create_max(maskWidth, *source_it);
  for (uint32_t i = 1; i < point_count.data; i++)
  {
    if (source_it[i] <= currentMaxVal)
      continue;

    auto range_size = i - range_start;
    auto index_into_random_offsets = (range_start + (range_size / 2)) % random_offsets.size();
    auto index = range_start + uint32_t(random_offsets[index_into_random_offsets] * (range_size));
    auto &to_lod = morton_to_lod.emplace_back();
    morton::morton_cast(source_it[index], min, to_lod.morton);
    to_lod.index.data = offset.data + index;
    to_lod.id = input_id;

    range_start = i;
    currentMaxVal = morton::create_max(maskWidth, source_it[i]);
  }
  auto index = range_start + ((point_count.data - range_start) / 2);
  auto &to_lod = morton_to_lod.emplace_back();
  to_lod.id = input_id;
  morton::morton_cast(source_it[index], min, to_lod.morton);
  to_lod.index.data = offset.data + index;
}

template <typename T, size_t N>
static void find_indecies_to_quantize(input_data_id_t input_id, const morton::morton192_t &min, type_t source_type, const buffer_t &source, offset_in_subset_t offset, point_count_t point_count, int maskWidth,
                                      const std::vector<float> &random_offsets, std::vector<morton_to_lod_t<T, N>> &morton_to_lod)
{
  assert(source_type == type_m32 || source_type == type_m64 || source_type == type_m128 || source_type == type_m192);

  switch (source_type)
  {
  case type_m32:
    return find_indecies_to_quantize<morton::morton32_t>(input_id, min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case type_m64:
    return find_indecies_to_quantize<morton::morton64_t>(input_id, min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case type_m128:
    return find_indecies_to_quantize<morton::morton128_t>(input_id, min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case type_m192:
    return find_indecies_to_quantize<morton::morton192_t>(input_id, min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  default:
    break;
  }
}
template <typename S_M, typename D_M>
static void quantize_morton_two(const morton::morton192_t &morton_min, const morton::morton192_t &morton_max, type_t source_type, const std::vector<uint32_t> &indecies_to_quantize, const buffer_t &source,
                                type_t destination_type, buffer_t &destination)
{
  (void)source_type;
  (void)destination_type;
  auto *source_it = reinterpret_cast<const S_M *>(source.data);
  auto *destination_it = reinterpret_cast<D_M *>(destination.data);
  assert(source.size % sizeof(S_M) == 0);
  assert(destination.size % sizeof(D_M) == 0);
  assert(indecies_to_quantize.back() < source.size / sizeof(S_M));
  for (uint32_t i = 0; i < uint32_t(indecies_to_quantize.size()); i++)
  {
    copy_morton<S_M, D_M>(source_it[indecies_to_quantize[i]], morton_min, morton_max, destination_it[i]);
  }
}

template <typename S_M>
static void quantize_morton_one(const morton::morton192_t &morton_min, const morton::morton192_t &morton_max, type_t source_type, const std::vector<uint32_t> &indecies_to_quantize, const buffer_t &source,
                                type_t destination_type, buffer_t &destination)
{
  assert(destination_type == type_m32 || destination_type == type_m64 || destination_type == type_m128 || destination_type == type_m192);
  switch (destination_type)
  {
  case type_m32:
    quantize_morton_two<S_M, morton::morton32_t>(morton_min, morton_max, source_type, indecies_to_quantize, source, destination_type, destination);
    break;
  case type_m64:
    quantize_morton_two<S_M, morton::morton64_t>(morton_min, morton_max, source_type, indecies_to_quantize, source, destination_type, destination);
    break;
  case type_m128:
    quantize_morton_two<S_M, morton::morton128_t>(morton_min, morton_max, source_type, indecies_to_quantize, source, destination_type, destination);
    break;
  case type_m192:
    quantize_morton_two<S_M, morton::morton192_t>(morton_min, morton_max, source_type, indecies_to_quantize, source, destination_type, destination);
    break;
  default:
    break;
  }
}

static buffer_t morton_buffer_for_subset(const buffer_t &buffer, type_t format, offset_in_subset_t offset, point_count_t count)
{
  auto format_byte_size = size_for_format(format);
  buffer_t ret;
  auto offset_bytes = offset.data * format_byte_size;
  ret.data = ((uint8_t *)buffer.data) + offset_bytes;
  ret.size = count.data * format_byte_size;
  assert(ret.size + offset.data <= buffer.size);
  return ret;
}

static bool buffer_is_subset(const buffer_t &super, const buffer_t &sub)
{
  return super.data <= sub.data && buffer_end(sub) <= buffer_end(super);
}

template <typename T, size_t S>
struct structured_data_t
{
  T data[S];
};

template <size_t S_S, typename S, size_t D_S, typename D>
void convert_points_impl(input_data_id_t input_id, const std::vector<std::pair<input_data_id_t, uint32_t>> &indecies, const point_format_t &source_format, const buffer_t &source, const point_format_t &destination_format,
                         buffer_t &destination)
{
  (void)source_format;
  (void)destination_format;
  using source_data_type_t = structured_data_t<S, S_S>;
  using destination_data_type_t = structured_data_t<D, D_S>;
  auto *source_begin = reinterpret_cast<const source_data_type_t *>(source.data);
  auto *destination_begin = reinterpret_cast<destination_data_type_t *>(destination.data);
  // assert(source.size % sizeof(source_data_type_t) == 0);
  assert(destination.size % sizeof(destination_data_type_t) == 0);
  assert(indecies.size() <= destination.size / sizeof(destination_data_type_t));

  constexpr auto copy_components = std::min(S_S, D_S);
  for (uint32_t destination_index = 0; destination_index < uint32_t(indecies.size()); destination_index++)
  {
    if (indecies[destination_index].first != input_id)
    {
      continue;
    }

    auto source_index = indecies[destination_index].second;
    for (int i = 0; i < int(copy_components); i++)
    {
      destination_begin[destination_index].data[i] = D(source_begin[source_index].data[i]);
    }
    for (int i = int(copy_components); i < int(D_S); i++)
    {
      destination_begin[destination_index].data[i] = D();
    }
  }
}

template <size_t S_S, typename S, size_t D_S>
void convert_points_three(input_data_id_t input_id, const std::vector<std::pair<input_data_id_t, uint32_t>> &indecies, const point_format_t &source_format, const buffer_t &source,
                          const point_format_t &destination_format, buffer_t &destination)
{
  switch (destination_format.type)
  {
  case type_u8:
    convert_points_impl<S_S, S, D_S, uint8_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_i8:
    convert_points_impl<S_S, S, D_S, int8_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_u16:
    convert_points_impl<S_S, S, D_S, uint16_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_i16:
    convert_points_impl<S_S, S, D_S, int16_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_u32:
    convert_points_impl<S_S, S, D_S, uint32_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_i32:
    convert_points_impl<S_S, S, D_S, int32_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_r32:
    convert_points_impl<S_S, S, D_S, float>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_u64:
    convert_points_impl<S_S, S, D_S, uint64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_i64:
    convert_points_impl<S_S, S, D_S, int64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_r64:
    convert_points_impl<S_S, S, D_S, double>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_m32:
    convert_points_impl<S_S, S, 1, uint32_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_m64:
    convert_points_impl<S_S, S, 1, uint64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_m128:
    convert_points_impl<S_S, S, 2, uint64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_m192:
    convert_points_impl<S_S, S, 3, uint64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  }
}

template <size_t S_S, typename S>
void convert_points_two(input_data_id_t input_id, const std::vector<std::pair<input_data_id_t, uint32_t>> &indecies, const point_format_t &source_format, const buffer_t &source, const point_format_t &destination_format,
                        buffer_t &destination)
{
  switch (destination_format.components)
  {
  case components_1:
    convert_points_three<S_S, S, 1>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case components_2:
    convert_points_three<S_S, S, 2>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case components_3:
    convert_points_three<S_S, S, 3>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case components_4:
    convert_points_three<S_S, S, 4>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case components_4x4:
    convert_points_three<S_S, S, 4 * 4>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  }
}
template <size_t S_S>
void convert_points_one(input_data_id_t input_id, const std::vector<std::pair<input_data_id_t, uint32_t>> &indecies, const point_format_t &source_format, const buffer_t &source, const point_format_t &destination_format,
                        buffer_t &destination)
{
  switch (source_format.type)
  {
  case type_u8:
    convert_points_two<S_S, uint8_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_i8:
    convert_points_two<S_S, int8_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_u16:
    convert_points_two<S_S, uint16_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_i16:
    convert_points_two<S_S, int16_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_u32:
    convert_points_two<S_S, uint32_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_i32:
    convert_points_two<S_S, int32_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_r32:
    convert_points_two<S_S, float>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_u64:
    convert_points_two<S_S, uint64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_i64:
    convert_points_two<S_S, int64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_r64:
    convert_points_two<S_S, double>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_m32:
    convert_points_two<1, uint32_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_m64:
    convert_points_two<1, uint64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_m128:
    convert_points_two<2, uint64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  case type_m192:
    convert_points_two<3, uint64_t>(input_id, indecies, source_format, source, destination_format, destination);
    break;
  }
}

static void copy_attribute_for_input(input_data_id_t input_id, const std::vector<std::pair<input_data_id_t, uint32_t>> &indecies, const point_format_t &source_format, const buffer_t &source_buffer,
                                     const point_format_t &target_format, buffer_t &target_buffer)
{
  switch (source_format.components)
  {
  case components_1:
    convert_points_one<1>(input_id, indecies, source_format, source_buffer, target_format, target_buffer);
    break;
  case components_2:
    convert_points_one<2>(input_id, indecies, source_format, source_buffer, target_format, target_buffer);
    break;
  case components_3:
    convert_points_one<3>(input_id, indecies, source_format, source_buffer, target_format, target_buffer);
    break;
  case components_4:
    convert_points_one<4>(input_id, indecies, source_format, source_buffer, target_format, target_buffer);
    break;
  case components_4x4:
    convert_points_one<4 * 4>(input_id, indecies, source_format, source_buffer, target_format, target_buffer);
    break;
  }
}

static void quantize_attributres(cache_file_handler_t &cache, const child_storage_map_t &child_storage_map, const std::vector<std::pair<input_data_id_t, uint32_t>> &indecies,
                                 const attribute_lod_mapping_t &lod_attrib_mapping, attribute_buffers_t &buffers)
{
  fixed_capacity_vector_t<input_data_id_t> inputs(indecies, [](const std::pair<input_data_id_t, uint32_t> &a) { return a.first; });
  std::sort(inputs.begin(), inputs.end());
  auto inputs_end = std::unique(inputs.begin(), inputs.end());
  for (auto inputs_it = inputs.begin(); inputs_it != inputs_end; ++inputs_it)
  {
    const auto &storage_info = child_storage_map.at(*inputs_it);
    auto mapping = lod_attrib_mapping.get_source_mapping(storage_info.attributes_id);
    for (int destination_buffer_index = 1; destination_buffer_index < int(buffers.buffers.size()); destination_buffer_index++)
    {
      auto target_buffer = buffers.buffers[destination_buffer_index];
      auto attr_mapping = mapping.source_attributes[destination_buffer_index];
      read_attribute_t source_attrib_data(cache, storage_info.locations[attr_mapping.source_index]);
      copy_attribute_for_input(*inputs_it, indecies, attr_mapping.source_format, source_attrib_data.data, lod_attrib_mapping.destination[destination_buffer_index], buffers.buffers[destination_buffer_index]);
    }
  }
}

template <typename T, size_t N>
static void quantize_subset(cache_file_handler_t &cache, const points_subset_t &subset, const lod_child_storage_info_t &storage_info, int lod, const std::vector<float> &random_offsets,
                            std::vector<morton_to_lod_t<T, N>> &morton_to_lod)
{
  read_only_points_t subset_data(cache, storage_info.locations[0]);
  offset_in_subset_t offset;
  point_count_t point_count;
  if (subset.count.data == uint32_t(0))
  {
    offset = offset_in_subset_t(0);
    point_count = point_count_t(subset_data.header.point_count);
  }
  else
  {
    offset = subset.offset;
    point_count = subset.count;
  }
  const buffer_t source_buffer = morton_buffer_for_subset(subset_data.data, subset_data.header.point_format.type, offset, point_count);

  assert(buffer_is_subset(subset_data.data, source_buffer));
  find_indecies_to_quantize(subset.input_id, subset_data.header.morton_min, subset_data.header.point_format.type, source_buffer, offset, point_count, lod - 3 * 3, random_offsets, morton_to_lod);
}

template <typename T, size_t N>
static void quantize_points_collection(cache_file_handler_t &cache, const points_collection_t &point_collection, const child_storage_map_t &child_storage_map, int lod, const std::vector<float> &random_offsets,
                                       std::vector<morton_to_lod_t<T, N>> &morton_to_lod)
{
  for (int i = 0; i < int(point_collection.data.size()); i++)
  {
    auto &subset = point_collection.data[i];
    const auto &storage = child_storage_map.at(subset.input_id);
    quantize_subset(cache, subset, storage, lod, random_offsets, morton_to_lod);
  }
}

struct calculate_child_buffer_size_t
{
  calculate_child_buffer_size_t(const std::vector<points_collection_t> &child_data)
  {
    for (auto &child : child_data)
    {
      storage_range_size += int(child.data.size());
      for (auto &subset : child.data)
      {
        morton_to_lod_size += int(subset.count.data / 8 + 1);
      }
    }
  }

  int morton_to_lod_size = 0;
  int storage_range_size = 0;
};

template <typename T, size_t N>
static void quantize_morton_remember_indecies_t(cache_file_handler_t &cache, const std::vector<points_collection_t> &child_data, const child_storage_map_t &child_storage_map, int lod,
                                                const std::vector<float> &random_offsets, std::unique_ptr<uint8_t[]> &morton_data, std::vector<std::pair<input_data_id_t, uint32_t>> &indecies)
{
  std::vector<morton_to_lod_t<T, N>> morton_to_lod;
  int maskWidth = lod - 3 * 3;
  {
    calculate_child_buffer_size_t child_buffer_sizes(child_data);
    morton_to_lod.reserve(child_buffer_sizes.morton_to_lod_size);
  }
  for (const auto &points_collection : child_data)
  {
    quantize_points_collection(cache, points_collection, child_storage_map, lod, random_offsets, morton_to_lod);
  }

  std::sort(morton_to_lod.begin(), morton_to_lod.end(), [](const morton_to_lod_t<T, N> &a, const morton_to_lod_t<T, N> &b) { return a.morton < b.morton; });

  morton_data.reset(new uint8_t[sizeof(morton::morton_t<T, N>) * morton_to_lod.size()]);
  auto target_morton_buffer = reinterpret_cast<morton::morton_t<T, N> *>(morton_data.get());
  uint32_t current_target_morton_buffer_index = 0;
  indecies.reserve(morton_to_lod.size());
  uint32_t range_start = 0;
  morton::morton_t<T, N> currentMaxVal = morton::create_max(maskWidth, morton_to_lod.front().morton);
  for (uint32_t i = 1; i < uint32_t(morton_to_lod.size()); i++)
  {
    if (morton_to_lod[i].morton <= currentMaxVal)
      continue;

    auto range_size = i - range_start;
    auto index = range_start + (range_size / 2);
    target_morton_buffer[current_target_morton_buffer_index++] = morton_to_lod[index].morton;
    indecies.emplace_back(morton_to_lod[index].id, uint32_t(morton_to_lod[index].index.data));

    range_start = i;
    currentMaxVal = morton::create_max(maskWidth, morton_to_lod[i].morton);
  }
  auto index = range_start + ((morton_to_lod.size() - range_start) / 2);
  target_morton_buffer[current_target_morton_buffer_index++] = morton_to_lod[index].morton;
  indecies.emplace_back(morton_to_lod[index].id, uint32_t(morton_to_lod[index].index.data));
}

static void quantize_morton_remember_indecies(cache_file_handler_t &cache, const std::vector<points_collection_t> &child_data, const child_storage_map_t &child_storage_map, int lod,
                                              const std::vector<float> &random_offsets, std::unique_ptr<uint8_t[]> &morton_data, std::vector<std::pair<input_data_id_t, uint32_t>> &indecies)
{
  auto lod_format = morton_format_from_lod(lod);
  switch (lod_format)
  {
  case type_m32:
    quantize_morton_remember_indecies_t<uint32_t, 1>(cache, child_data, child_storage_map, lod, random_offsets, morton_data, indecies);
    break;
  case type_m64:
    quantize_morton_remember_indecies_t<uint64_t, 1>(cache, child_data, child_storage_map, lod, random_offsets, morton_data, indecies);
    break;
  case type_m128:
    quantize_morton_remember_indecies_t<uint64_t, 2>(cache, child_data, child_storage_map, lod, random_offsets, morton_data, indecies);
    break;
  case type_m192:
    quantize_morton_remember_indecies_t<uint64_t, 3>(cache, child_data, child_storage_map, lod, random_offsets, morton_data, indecies);
    break;
  default:
    assert("This should not happen");
  }
}

void lod_worker_t::work()
{
  attributes_t attributes;
  std::unique_ptr<attributes_id_t[]> attribute_ids(new attributes_id_t[data.child_storage_info.size()]);
  int child_data_count = 0;
  for (auto const &storage_info : data.child_storage_info)
  {
    attribute_ids[child_data_count++] = storage_info.second.attributes_id;
  }

  auto lod_format = morton_format_from_lod(data.lod);
  auto lod_attrib_mapping = attributes_configs.get_lod_attribute_mapping(data.lod, attribute_ids.get(), attribute_ids.get() + data.child_storage_info.size());

  storage_header_t destination_header;
  storage_header_initialize(destination_header);
  destination_header.input_id = data.storage_name;
  attribute_buffers_t buffers;

  std::vector<std::pair<input_data_id_t, uint32_t>> indecies;
  {
    std::unique_ptr<uint8_t[]> morton_attribute_buffer;
    quantize_morton_remember_indecies(cache, data.child_data, data.child_storage_info, data.lod, random_offsets, morton_attribute_buffer, indecies);
    attribute_buffers_initialize(lod_attrib_mapping.destination, buffers, uint32_t(indecies.size()), std::move(morton_attribute_buffer));
  }

  quantize_attributres(cache, data.child_storage_info, indecies, lod_attrib_mapping, buffers);

  destination_header.morton_min = data.node_min;
  destination_header.morton_max = morton::morton_or(data.node_min, morton::morton_mask_create<uint64_t, 3>(data.lod));

  assert(!indecies.empty());

  attribute_buffers_adjust_buffers_to_size(lod_attrib_mapping.destination, buffers, uint32_t(indecies.size()));
  destination_header.point_count = uint32_t(indecies.size());
  destination_header.point_format = {lod_format, components_1};
  destination_header.lod_span = data.lod;
  cache.write(destination_header, lod_attrib_mapping.destination_id, std::move(buffers), [this](const storage_header_t &storageheader, attributes_id_t attrib_id, std::vector<storage_location_t> locations, const error_t &error)
              {
                (void)storageheader;
                (void)error;
                this->data.generated_attributes_id = attrib_id;
                this->data.generated_locations = std::move(locations);
                this->lod_generator.add_worker_done(this->batch);
              });
  data.generated_point_count.data = uint32_t(indecies.size());
}

void lod_worker_t::after_work(completion_t completion)
{
    (void)completion;
}

static void get_storage_info(tree_cache_t &tree_cache, lod_node_worker_data_t &node)
{
  for (int i = 0; i < int(node.child_data.size()); i++)
  {
    auto tree_id = node.child_trees[i];
    auto tree = tree_cache.get(tree_id);
    for (int j = 0; j < int(node.child_data[i].data.size()); j++)
    {
      auto &child_data = node.child_data[i].data[j];

      auto &storage_info = node.child_storage_info[child_data.input_id];
      if (storage_info.locations.empty())
      {
        auto info = tree->storage_map.info(child_data.input_id);
        storage_info.attributes_id = info.first;
        storage_info.locations = std::move(info.second);
      }
    }
  }
}

static void adjust_tree_after_lod(tree_cache_t &tree_cache, std::vector<lod_tree_worker_data_t> &to_adjust, int level)
{
  for (auto &adjust_data : to_adjust)
  {
    tree_t *tree = tree_cache.get(adjust_data.tree_id);
    if (adjust_data.nodes[level].empty())
      continue;
    int tree_index = 0;
    for (int node_index = 0; node_index < int(adjust_data.nodes[level].size()); node_index++)
    {
      auto current = adjust_data.nodes[level][node_index].id;
      auto &node_ids = tree->node_ids[level];

      while (tree_index < int(node_ids.size()) && node_ids[tree_index] < current)
        tree_index++;
      assert(node_ids[tree_index] == current);
      auto &done_node = adjust_data.nodes[level][node_index];
      tree->data[level][tree_index].point_count = done_node.generated_point_count.data;
      tree->storage_map.add_storage(done_node.storage_name, done_node.generated_attributes_id, std::move(done_node.generated_locations));
    }
  }
}

static void iterate_batch(const std::vector<float> &random_offsets, tree_lod_generator_t &lod_generator, lod_worker_batch_t &batch, tree_cache_t &tree_cache, cache_file_handler_t &cache_file,
                          attributes_configs_t &attributes_configs, threaded_event_loop_t &loop)
{
  if (!batch.new_batch)
    adjust_tree_after_lod(tree_cache, batch.worker_data, batch.level);

  batch.new_batch = false;
  batch.lod_workers.clear();
  batch.level--;
  batch.completed = 0;

  size_t batch_size = 0;
  while (batch_size == 0 && batch.level >= 0)
  {
    for (auto &tree : batch.worker_data)
      batch_size += tree.nodes[batch.level].size();
    if (batch_size == 0)
      batch.level--;
  }
  batch.batch_size = int(batch_size);

  batch.lod_workers.reserve(batch_size);

  for (auto &tree : batch.worker_data)
  {
    for (auto &node : tree.nodes[batch.level])
    {
      assert(!node.child_data.empty());
      get_storage_info(tree_cache, node);
      auto &lod_worker = batch.lod_workers.emplace_back(lod_generator, batch, cache_file, attributes_configs, node, random_offsets);
      lod_worker.enqueue(loop);
    }
  }
}

tree_lod_generator_t::tree_lod_generator_t(threaded_event_loop_t &loop, const tree_global_state_t &tree_global_state, tree_cache_t &tree_cache, cache_file_handler_t &file_cache, attributes_configs_t &attributes_configs)
  : _loop(loop)
  , _tree_global_state(tree_global_state)
  , _tree_cache(tree_cache)
  , _file_cache(file_cache)
  , _attributes_configs(attributes_configs)
  , _iterate_workers(_loop, event_bind_t::bind(*this, &tree_lod_generator_t::iterate_workers))
{
  _random_offsets.resize(256);
  std::mt19937 gen(4244);
  std::uniform_real_distribution<float> dis(0.0, 1.0);
  for (auto &offset : _random_offsets)
  {
    offset = dis(gen);
  }
}

void tree_lod_generator_t::generate_lods(tree_id_t &tree_id, const morton::morton192_t &max)
{
  (void)max;
  // auto &worker_data = batch.worker_data;
  std::vector<lod_tree_worker_data_t> to_lod;
  lod_node_worker_data_t fake_parent;
  fake_parent.node_min = _tree_cache.data[tree_id.data].morton_min;
  tree_get_work_items(_tree_cache, _file_cache, tree_id, fake_parent, to_lod);
  if (!to_lod.empty())
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
    if (to_lod.begin() + batch_start != to_lod.end())
    {
      _lod_batches.emplace_back(new lod_worker_batch_t());
      auto &batch = *_lod_batches.back();
      batch.worker_data.insert(batch.worker_data.end(), std::make_move_iterator(to_lod.begin() + batch_start), std::make_move_iterator(to_lod.end()));
    }
  }
  iterate_workers();
}


void tree_lod_generator_t::iterate_workers()
{
  if (!_lod_batches.empty() && _lod_batches.front()->completed == int(_lod_batches.front()->lod_workers.size()) && _lod_batches.front()->level == 0)
  {
    adjust_tree_after_lod(_tree_cache, _lod_batches.front()->worker_data, 0);
    _lod_batches.pop_front();
  }
  if (!_lod_batches.empty() && (_lod_batches.front()->new_batch || _lod_batches.front()->completed == int(_lod_batches.front()->lod_workers.size())))
    iterate_batch(_random_offsets, *this, *_lod_batches.front(), _tree_cache, _file_cache, _attributes_configs, _loop);
  if (_lod_batches.empty())
  {
    fmt::print(stderr, "Done\n");
  }
}


} // namespace points::converter
