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
    int to_ret = data.data.size();
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
      morton::morton_set_child_mask(lod, i, child_min);
      if (max < child_min)
        break;
      morton::morton192_t child_max = parent_max;
      morton::morton_set_child_mask(lod, i, child_max);
      if (child_max < min)
        continue;
      int sub_skip = sub_skip_parent + child_count - 1;
      std::pair<int,int> adjust;
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

static void tree_get_work_items(tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t &tree_id, const morton::morton192_t &min, const morton::morton192_t &max, std::vector<lod_worker_data_t> &lod_data)
{
  auto tree = tree_cache.get(tree_id);
  children_subset_t to_lod;
  find_missing_lod(tree_cache, cache, tree_id, min, max, tree->morton_min, tree->morton_max, 0,0, to_lod);

  lod_data.reserve(to_lod.skips.size() / 3);

  std::vector<int> parent_end_stack;
  parent_end_stack.reserve(30);
  std::vector<int> parent_stack;
  parent_stack.reserve(30);
  auto data_start = to_lod.data.begin();
  for (int i = 0; i < int(to_lod.skips.size()); i++)
  {
    int current_skip = int(to_lod.skips[i]);
    bool leaf_node = current_skip == 1;
    int data_skip = leaf_node ? to_lod.data_skips[i] : 1;
    if (parent_stack.size())
    {
      auto &parent = lod_data[parent_stack.back()];
      parent.child_data.insert(parent.child_data.end(), data_start, data_start + data_skip);
    }
    if (!leaf_node)
    {
      parent_stack.emplace_back(int(lod_data.size()));
      parent_end_stack.emplace_back(i + current_skip - 1);
      lod_data.emplace_back(to_lod.lods[i], *data_start);
    }
    while(parent_end_stack.size() && parent_end_stack.back() == i)
    {
      parent_stack.pop_back();
      parent_end_stack.pop_back();
    }
    data_start += data_skip;
  }
  std::sort(lod_data.begin(), lod_data.end(), [](const lod_worker_data_t &a, const lod_worker_data_t &b)
  {
    return a.lod < b.lod;
  });
  fmt::print("{}\n", to_lod.data.size());
}

lod_worker_t::lod_worker_t(tree_lod_generator_t &lod_generator, cache_file_handler_t &cache, attributes_configs_t &attributes_configs, lod_worker_data_t &data)
  : lod_generator(lod_generator)
  , cache(cache)
  , attributes_configs(attributes_configs)
  , data(data)
{
  (void)this->cache;
  (void)this->data;
}

template<typename T, size_t S>
struct structured_data_t
{
  T data[S];
};

template<size_t S_S, typename S, size_t D_S, typename D>
uint32_t convert_points_impl(uint32_t step, const std::pair<type_t, components_t> &source_format, const buffer_t &source, const std::pair<type_t, components_t> &destination_format, buffer_t &destination)
{
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
      destination_it->data[i] = source_it->data[i];
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
  }
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
  }
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
static typename std::enable_if<greater_than<sizeof(S_M), sizeof(D_M)>::value, S_M>::type copy_morton(const S_M &s, const morton::morton192_t &morton_min, D_M &d)
{
  (void)s;
  (void)morton_min;
  (void)d;
  assert(false);
}

template<typename S_M, typename D_M>
static uint32_t quantize_morton_two(uint32_t step, const morton::morton192_t &morton_min, type_t source_type, const buffer_t &source, type_t destination_type, buffer_t &destination)
{
  assert(sizeof(S_M) <= sizeof(D_M));
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

uint32_t quantize_morton(uint32_t step, const morton::morton192_t &morton_min, type_t source_type, const buffer_t &source, type_t destination_type, buffer_t &destination)
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

buffer_t morton_buffer_for_subset(const buffer_t &buffer, type_t format, offset_t offset)
{
  int format_byte_size = size_for_format(format);
  buffer_t ret;
  auto offset_bytes = offset.data * format_byte_size;
  ret.data = ((uint8_t *)buffer.data) + offset_bytes;
  ret.size = buffer.size - offset_bytes;
  return ret;
}
buffer_t buffer_for_subset(const buffer_t &buffer, std::pair<type_t, components_t> format, offset_t offset)
{
  int format_byte_size = size_for_format(format.first, format.second);
  buffer_t ret;
  auto offset_bytes = offset.data * format_byte_size;
  ret.data = ((uint8_t *)buffer.data) + offset_bytes;
  ret.size = buffer.size - offset_bytes;
  return ret;
}

bool buffer_is_subset(const buffer_t &super, const buffer_t &sub)
{
  return super.data <= sub.data && buffer_end(sub) <= buffer_end(super);
}

uint32_t quantize_to_parent(const points_subset_t &child, uint32_t count, cache_file_handler_t &file_cache, const std::vector<std::pair<type_t, components_t>> &destination_map, const attribute_lod_info_t &source_maping, attribute_buffers_t &destination_buffers, offset_t destination_offset)
{
  auto &source_map = source_maping.source_attributes;
  assert(destination_map.size() == source_map.size()
         && destination_map.size() == destination_buffers.buffers.size());
  assert(count <= child.count.data);
  uint32_t step = child.count.data / count;
  (void)step;
  uint32_t quantized_morton = 0;
  {
    read_points_t child_data(file_cache, child.input_id, source_map[0].index);
    const buffer_t source_buffer = morton_buffer_for_subset(child_data.data,child_data.header.point_format, child.offset);
    assert(buffer_is_subset(child_data.data, source_buffer));
    buffer_t  destination_buffer = buffer_for_subset(destination_buffers.buffers[0], destination_map[0], destination_offset);
    assert(buffer_is_subset(destination_buffers.buffers[0], destination_buffer));
    quantized_morton = quantize_morton(step, child_data.header.morton_min, child_data.header.point_format, source_buffer, destination_map[0].first, destination_buffer);
  }
  for (int i = 1; i < int(destination_map.size()); i++)
  {
    read_points_t child_data(file_cache, child.input_id, source_map[i].index);
    const buffer_t source_buffer = buffer_for_subset(child_data.data,source_map[i].format, child.offset);
    assert(buffer_is_subset(child_data.data, source_buffer));
    buffer_t  destination_buffer = buffer_for_subset(destination_buffers.buffers[i], destination_map[i], destination_offset);
    assert(buffer_is_subset(destination_buffers.buffers[i], destination_buffer));
    auto quantized_attribute = quantize_points(step, source_map[i].format, source_buffer, destination_map[i], destination_buffer);
    assert(quantized_attribute == quantized_morton);
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
    total_count += child.count.data;
    bool got_attrib = cache.attribute_id_for_input_id(child.input_id, attribute_ids[i]);
    (void) got_attrib;
    assert(got_attrib);
  }
  std::unique_ptr<attributes_id_t[]> attribute_ids_sorted(new attributes_id_t[data.child_data.size()]);
  memcpy(attribute_ids_sorted.get(), attribute_ids.get(), data.child_data.size() * sizeof(*(attribute_ids_sorted.get())));
  auto attrib_begin = attribute_ids_sorted.get();
  auto attrib_end = attrib_begin + data.child_data.size();
  std::sort(attrib_begin, attrib_end, [](const attributes_id_t &a, const attributes_id_t &b) { return a.data < b.data; });
  attrib_end = std::unique(attrib_begin, attrib_end, [](const attributes_id_t &a, const attributes_id_t &b) { return a.data == b.data; });

  auto lod_format = morton_format_from_lod(data.lod);

  auto lod_attrib_mapping = attributes_configs.get_lod_attribute_mapping(lod_format, attrib_begin, attrib_end);

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
      total_acc_count.data += quantize_to_parent(child, child_count, cache, lod_attrib_mapping.destination, source_mapping, buffers, total_acc_count);
      (void) attributes_configs;
    }
  }
  attribute_buffers_adjust_buffers_to_size(lod_attrib_mapping.destination, buffers, total_acc_count.data);
  fmt::print("Total count {}, accumulated count {}\n", total_count, total_acc_count.data);
}

void lod_worker_t::after_work(completion_t completion)
{
  lod_generator.iterate_workers();
}

static void iterate_batch(tree_lod_generator_t &lod_generator, lod_worker_batch_t &batch, cache_file_handler_t &cache_file, attributes_configs_t &attributes_configs, threaded_event_loop_t &loop)
{
  assert(batch.completed < int(batch.worker_data.size()));
  if (batch.current_index == int(batch.worker_data.size()))
    return;
  batch.lod_workers.clear();
  int current_lod = batch.worker_data[batch.current_index].lod;
  int batch_index = batch.current_index;
  while(batch_index < int(batch.worker_data.size())
        && current_lod == batch.worker_data[batch.current_index].lod)
    batch_index++;
  int batch_size = batch_index - batch.current_index;
  batch.lod_workers.reserve(batch_size);
  while(batch.current_index < int(batch.worker_data.size())
        && current_lod == batch.worker_data[batch.current_index].lod)
  {
    batch.lod_workers.emplace_back(lod_generator, cache_file, attributes_configs, batch.worker_data[batch.current_index]);
    batch.current_index++;
    batch.lod_workers.back().enqueue(loop);
  }
}

tree_lod_generator_t::tree_lod_generator_t(threaded_event_loop_t &loop, const tree_global_state_t &tree_global_state, tree_cache_t &tree_cache, cache_file_handler_t &file_cache, attributes_configs_t &attributes_configs)
  : _loop(loop)
  , _tree_global_state(tree_global_state)
  , _tree_cache(tree_cache)
  , _file_cache(file_cache)
  , _attributes_configs(attributes_configs)
{
}

void tree_lod_generator_t::generate_lods(tree_id_t &tree_id, const morton::morton192_t &max)
{
  _lod_batches.emplace_back(new lod_worker_batch_t());
  auto &batch = *_lod_batches.back();
  auto &worker_data = batch.worker_data;
  tree_get_work_items(_tree_cache, _file_cache, tree_id, _generated_until, max, worker_data);
  iterate_workers();
}

void tree_lod_generator_t::iterate_workers()
{
  if (_lod_batches.size() && _lod_batches.front()->completed == int(_lod_batches.front()->worker_data.size()))
  {
    _lod_batches.erase(_lod_batches.begin());
  }
  if (_lod_batches.size() && _lod_batches.front()->completed == _lod_batches.front()->current_index)
    iterate_batch(*this, *_lod_batches.front(), _file_cache, _attributes_configs, _loop);

}

}}//namespace
