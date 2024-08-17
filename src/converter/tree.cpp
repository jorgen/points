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
#include "tree.hpp"
#include "memory_writer.hpp"
#include "point_buffer_splitter.hpp"
#include <cassert>

namespace points::converter
{

tree_t &tree_cache_create_root_tree(tree_registry_t &tree_cache)
{
  tree_cache.data.emplace_back();
  tree_cache.locations.emplace_back(0, 0, 0);
  tree_cache.tree_id_initialized.emplace_back(true);
  tree_cache.data.back().id.data = tree_cache.current_id++;
  return tree_cache.data.back();
}

tree_t &tree_cache_add_tree(tree_registry_t &tree_cache, tree_t *(&parent))
{
  auto id = parent->id;
  tree_cache.data.emplace_back();
  tree_cache.locations.emplace_back(0, 0, 0);
  tree_cache.tree_id_initialized.emplace_back(true);
  tree_cache.data.back().id.data = tree_cache.current_id++;
  parent = &tree_cache.data[id.data];
  return tree_cache.data.back();
}

tree_id_t tree_initialize(tree_registry_t &tree_registry, storage_handler_t &cache, const storage_header_t &header, attributes_id_t attributes, std::vector<storage_location_t> &&locations)
{
  tree_t &tree = tree_cache_create_root_tree(tree_registry);
  morton::morton192_t mask = morton::morton_xor(header.morton_min, header.morton_max);
  int magnitude = morton::morton_magnitude_from_bit_index(morton::morton_msb(mask));
  morton::morton192_t new_tree_mask = morton::morton_mask_create<uint64_t, 3>(morton::morton_magnitude_to_lod(magnitude));
  morton::morton192_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  tree.morton_min = morton::morton_and(header.morton_min, new_tree_mask_inv);
  tree.morton_max = morton::morton_or(header.morton_min, new_tree_mask);
  tree.is_dirty = true;
  tree.magnitude = uint8_t(magnitude);
  tree.nodes[0].push_back(0);
  tree.skips[0].push_back(int16_t(0));
  tree.data[0].emplace_back();
  uint16_t root_name = morton::morton_get_name(0, 0, morton::morton_get_child_mask(morton::morton_magnitude_to_lod(magnitude) + 1, header.morton_min));
  tree.node_ids[0].emplace_back(root_name);
#ifndef NDEBUG
  tree.mins[0].push_back(tree.morton_min);
#endif

  auto id = tree_add_points(tree_registry, cache, tree.id, header, attributes, std::move(locations));
  return id;
}

static void tree_initialize_sub(const tree_t &parent_tree, tree_registry_t &tree_cache, const morton::morton192_t &morton, tree_t &sub_tree)
{
  (void)tree_cache;
  sub_tree.magnitude = parent_tree.magnitude - 1;
  morton::morton192_t new_tree_mask = morton::morton_mask_create<uint64_t, 3>(morton::morton_magnitude_to_lod(sub_tree.magnitude));
  morton::morton192_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  sub_tree.morton_min = morton::morton_and(morton, new_tree_mask_inv);
  sub_tree.morton_max = morton::morton_or(morton, new_tree_mask);

  sub_tree.nodes[0].push_back(0);
  sub_tree.skips[0].push_back(int16_t(0));
  sub_tree.data[0].emplace_back();
  uint16_t root_name = morton::morton_get_name(0, 0, morton::morton_get_child_mask(morton::morton_magnitude_to_lod(sub_tree.magnitude) + 1, morton));
  sub_tree.node_ids[0].emplace_back(root_name);
#ifndef NDEBUG
  sub_tree.mins[0].push_back(sub_tree.morton_min);
#endif
}

static void tree_initialize_new_parent(const tree_t &some_child, const morton::morton192_t possible_min, const morton::morton192_t possible_max, tree_t &new_parent)
{
  morton::morton192_t new_min = some_child.morton_min < possible_min ? some_child.morton_min : possible_min;
  morton::morton192_t new_max = some_child.morton_max < possible_max ? possible_max : some_child.morton_max;

  int lod = morton::morton_lod(new_min, new_max);
  new_parent.magnitude = uint8_t(morton::morton_magnitude_from_lod(uint8_t(lod)));
  morton::morton192_t new_tree_mask = morton::morton_mask_create<uint64_t, 3>(lod);
  morton::morton192_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  new_parent.morton_min = morton::morton_and(new_tree_mask_inv, new_min);
  new_parent.morton_max = morton::morton_or(new_parent.morton_min, new_tree_mask);
  new_parent.nodes[0].push_back(0);
  new_parent.skips[0].push_back(int16_t(0));
  new_parent.data[0].emplace_back();
}

static int sub_tree_count_skips(uint8_t node, int index)
{
  int node_skips = 0;
  for (int i = 0; i < index; i++)
  {
    if (node & uint8_t(1 << i))
      node_skips++;
  }
  return node_skips;
}

static void sub_tree_alloc_children(tree_t &tree, int level, int skip)
{
  assert(skip <= int(tree.skips[level].size()));
  int old_skip = 0;
  if (skip < int(tree.skips[level].size()))
    old_skip = tree.skips[level][skip];
  else if (skip > 0 && skip == int(tree.skips[level].size()))
    old_skip = tree.skips[level].back() + sub_tree_count_skips(tree.nodes[level].back(), 8);

  tree.nodes[level].emplace(tree.nodes[level].begin() + skip);
  tree.skips[level].emplace(tree.skips[level].begin() + skip, uint16_t(old_skip));
  tree.data[level].emplace(tree.data[level].begin() + skip);
  tree.node_ids[level].emplace(tree.node_ids[level].begin() + skip);
#ifndef NDEBUG
  tree.mins[level].emplace(tree.mins[level].begin() + skip);
#endif
}

static void sub_tree_increase_skips(tree_t &tree, int level, int skip)
{
  auto &skips = tree.skips[level];
  auto skips_size = skips.size();
  for (int i = skip + 1; i < int(skips_size); i++)
  {
    skips[i]++;
  }
}

static void sub_tree_split_points_to_children(storage_handler_t &cache, input_storage_map_t &storage_map, points_collection_t &&points, int lod, const morton::morton192_t &node_min, points_collection_t (&children)[8])
{
  deref_on_destruct_t to_deref(storage_map);
  for (auto &p : points.data)
  {
    to_deref.add(p.input_id);
    read_only_points_t p_read(cache, storage_map.location(p.input_id, 0));
    assert(p_read.data.size);

    point_buffer_subdivide(p_read, storage_map, p, lod, node_min, children);
  }
  points = points_collection_t();
}

static void move_storage_locations_to_subtree(const points_collection_t &collection, tree_t &parent, tree_t &sub_tree)
{
  for (auto &p : collection.data)
  {
    auto attrib_locations_pair = parent.storage_map.dereference(p.input_id);
    sub_tree.storage_map.add_storage(p.input_id, attrib_locations_pair.first, std::move(attrib_locations_pair.second));
  }
}

static void sub_tree_insert_points(tree_registry_t &tree_cache, storage_handler_t &cache, tree_id_t tree_id, const morton::morton192_t &min, int current_level, int skip, uint16_t current_name,
                                   points_collection_t &&points) // NOLINT(*-no-recursion)
{
  auto *tree = tree_cache.get(tree_id);
  tree->is_dirty = true;
  assert(tree->id.data < tree_cache.current_id);
  assert(current_level != 0 || tree->morton_min == min);
  assert(tree->mins[current_level][skip] == min);
  assert(tree->node_ids[current_level][skip] == current_name);
  assert(skip == 0 || tree->mins[current_level][skip - 1] < tree->mins[current_level][skip]);
  assert(int(tree->mins[current_level].size() - 1) == skip || tree->mins[current_level][skip] < tree->mins[current_level][skip + 1]);

  auto &node = tree->nodes[current_level][skip];
  int lod = morton::morton_tree_level_to_lod(tree->magnitude, current_level);
  auto child_mask = morton::morton_get_child_mask(lod, points.min);
  assert(child_mask < 8);
  assert(!(points.min < min));
  assert(!(morton::morton_or(min, morton::morton_mask_create<uint64_t, 3>(lod)) < points.max));
  assert(morton::get_name_from_morton(lod, points.min) == current_name);
  assert(morton::get_name_from_morton(lod, tree->mins[current_level][skip]) == current_name);
  if (lod > points.min_lod)
  {
    morton::morton192_t new_min = min;
    morton::morton_set_child_mask(lod, child_mask, new_min);
    int sub_skip = tree->skips[current_level][skip] + sub_tree_count_skips(node, child_mask);
    if (node & (1 << child_mask))
    {
      if (current_level == 4)
      {
        uint16_t sub_tree_name = morton::morton_get_name(0, 0, child_mask);
        auto sub_tree_id = tree->sub_trees[sub_skip];
        auto *sub_tree = tree_cache.get(sub_tree_id);
        move_storage_locations_to_subtree(points, *tree, *sub_tree);
        sub_tree_insert_points(tree_cache, cache, sub_tree_id, new_min, 0, 0, sub_tree_name, std::move(points));
      }
      else
      {
        auto child_name = morton::morton_get_name(current_name, current_level + 1, child_mask);
        sub_tree_insert_points(tree_cache, cache, tree_id, new_min, current_level + 1, sub_skip, child_name, std::move(points));
      }
      return;
    }
    else if (node)
    {

      node |= uint8_t(1) << child_mask;

      if (current_level == 4)
      {
        auto &sub_tree = tree_cache_add_tree(tree_cache, tree);
        tree->sub_trees.emplace(tree->sub_trees.begin() + sub_skip, sub_tree.id);
        sub_tree_increase_skips(*tree, current_level, skip);
        tree_initialize_sub(*tree, tree_cache, points.min, sub_tree);
        uint16_t sub_tree_name = morton::morton_get_name(0, 0, child_mask);
        move_storage_locations_to_subtree(points, *tree, sub_tree);
        sub_tree_insert_points(tree_cache, cache, sub_tree.id, new_min, 0, 0, sub_tree_name, std::move(points));
      }
      else
      {
        sub_tree_alloc_children(*tree, current_level + 1, sub_skip);
        sub_tree_increase_skips(*tree, current_level, skip);
        auto child_name = morton::morton_get_name(current_name, current_level + 1, child_mask);
        tree->node_ids[current_level + 1][sub_skip] = child_name;
#ifndef NDEBUG
        tree->mins[current_level + 1][sub_skip] = new_min;
#endif
        sub_tree_insert_points(tree_cache, cache, tree_id, new_min, current_level + 1, sub_skip, child_name, std::move(points));
      }
      return;
    }
  }

  if (node == 0 && tree->data[current_level][skip].point_count + points.point_count <= tree_cache.node_limit)
  {
    assert(!(points.min < min));
    points_data_add(tree->data[current_level][skip], std::move(points));
    return;
  }
  assert(points.point_count);
  assert(tree->magnitude > 0 || current_level < 4);
  {
    points_collection_t children_data[8];
    if (!node && tree->data[current_level][skip].point_count)
    {
      sub_tree_split_points_to_children(cache, tree->storage_map, std::move(tree->data[current_level][skip]), lod, min, children_data);
      tree->data[current_level][skip].point_count = 0;
    }
    if (points.point_count)
    {
      sub_tree_split_points_to_children(cache, tree->storage_map, std::move(points), lod, min, children_data);
    }

    int child_count = 0;
    for (int i = 0; i < 8; i++)
    {
      auto &child_data = children_data[i];
      const bool has_this_child = node & (1 << i);
      if (child_data.data.empty())
      {
        if (has_this_child)
          child_count++;
        continue;
      }
      morton::morton192_t new_min = min;
      morton::morton_set_child_mask(lod, uint8_t(i), new_min);
      assert(child_data.min_lod <= lod);

      tree = tree_cache.get(tree_id);
      int sub_skip = tree->skips[current_level][skip] + child_count;

      if (current_level == 4)
      {
        uint16_t sub_tree_name = morton::morton_get_name(0, 0, i);
        if (!has_this_child)
        {
          node |= uint8_t(1) << i;
          auto &sub_tree = tree_cache_add_tree(tree_cache, tree);
          tree->sub_trees.emplace(tree->sub_trees.begin() + sub_skip, sub_tree.id);
          sub_tree_increase_skips(*tree, current_level, skip);
          tree_initialize_sub(*tree, tree_cache, child_data.min, sub_tree);
          move_storage_locations_to_subtree(child_data, *tree, sub_tree);
          sub_tree_insert_points(tree_cache, cache, sub_tree.id, new_min, 0, 0, sub_tree_name, std::move(child_data));
        }
        else
        {
          tree_t *sub_tree = tree_cache.get(tree->sub_trees[sub_skip]);
          move_storage_locations_to_subtree(child_data, *tree, *sub_tree);
          sub_tree_insert_points(tree_cache, cache, sub_tree->id, new_min, 0, 0, sub_tree_name, std::move(child_data));
        }
      }
      else
      {
        auto child_name = morton::morton_get_name(current_name, current_level + 1, i);
        if (has_this_child)
        {
          sub_tree_insert_points(tree_cache, cache, tree_id, new_min, current_level + 1, sub_skip, child_name, std::move(child_data));
        }
        else
        {
          node |= uint8_t(1) << i;
          sub_tree_alloc_children(*tree, current_level + 1, sub_skip);
          sub_tree_increase_skips(*tree, current_level, skip);
          tree->node_ids[current_level + 1][sub_skip] = child_name;
#ifndef NDEBUG
          tree->mins[current_level + 1][sub_skip] = new_min;
#endif
          sub_tree_insert_points(tree_cache, cache, tree_id, new_min, current_level + 1, sub_skip, child_name, std::move(child_data));
        }
      }
      child_count++;
    }
  }
}

static void insert_tree_in_tree(tree_registry_t &tree_registry, tree_id_t &parent_id, const tree_id_t &child_id) // NOLINT(*-no-recursion)
{
  tree_t *parent = tree_registry.get(parent_id);
  tree_t *child = tree_registry.get(child_id);
  assert(memcmp(morton::morton_and(morton::morton_negate(morton::morton_xor(parent->morton_min, parent->morton_max)), child->morton_min).data, parent->morton_min.data, sizeof(parent->morton_min)) == 0);
  int current_skip = 0;
  int lod = morton::morton_magnitude_to_lod(parent->magnitude);
  for (int i = 0; i < 4; i++, lod--)
  {
    auto &node = parent->nodes[i][current_skip];
    auto child_mask = morton::morton_get_child_mask(lod, child->morton_min);
    int node_skips = sub_tree_count_skips(node, child_mask);
    if (node & (1 << child_mask))
    {
      current_skip = parent->skips[i][current_skip] + node_skips;
    }
    else
    {
      node |= 1 << child_mask;
      sub_tree_alloc_children(*parent, i + 1, parent->skips[i][current_skip] + node_skips);
      sub_tree_increase_skips(*parent, i, current_skip);
      current_skip = parent->skips[i][current_skip] + node_skips;
    }
  }

  auto &node = parent->nodes[4][current_skip];
  auto child_mask = morton::morton_get_child_mask(lod, child->morton_min);
  int node_skips = sub_tree_count_skips(node, child_mask);
  int current_skip_and_node = current_skip + node_skips;
  if (node & (1 << child_mask))
  {
    auto *sub_tree = tree_registry.get(parent->sub_trees[current_skip_and_node]);
    insert_tree_in_tree(tree_registry, sub_tree->id, child->id);
  }
  else
  {
    node |= 1 << child_mask;
    if (parent->magnitude - 1 == child->magnitude)
    {
      parent->sub_trees.emplace(parent->sub_trees.begin() + current_skip_and_node, child_id);
      sub_tree_increase_skips(*parent, 4, current_skip);
    }
    else
    {
      auto &sub_tree = tree_cache_add_tree(tree_registry, parent);
      parent = tree_registry.get(parent_id);
      child = tree_registry.get(child_id);
      parent->sub_trees.emplace(parent->sub_trees.begin() + current_skip_and_node, sub_tree.id);
      sub_tree_increase_skips(*parent, 4, current_skip);
      tree_initialize_sub(*parent, tree_registry, child->morton_min, sub_tree);
      insert_tree_in_tree(tree_registry, sub_tree.id, child->id);
    }
  }
}

static tree_id_t reparent_tree(tree_registry_t &tree_registry, tree_id_t tree_id, const morton::morton192_t &possible_min, const morton::morton192_t &possible_max)
{
  tree_t *tree = tree_registry.get(tree_id);
  tree_t &new_parent = tree_cache_add_tree(tree_registry, tree);
  tree_initialize_new_parent(*tree, possible_min, possible_max, new_parent);
  assert(new_parent.magnitude != tree->magnitude);

  insert_tree_in_tree(tree_registry, new_parent.id, tree->id);
  return new_parent.id;
}

tree_id_t tree_add_points(tree_registry_t &tree_registry, storage_handler_t &cache, const tree_id_t &tree_id, const storage_header_t &header, attributes_id_t attributes_id, std::vector<storage_location_t> &&locations)
{
  tree_id_t ret = tree_id;
  auto *tree = tree_registry.get(tree_id);
  // assert(validate_points_offset(header));
  if (header.morton_min < tree->morton_min || tree->morton_max < header.morton_max)
  {
    ret = reparent_tree(tree_registry, tree_id, header.morton_min, header.morton_max);
    tree = tree_registry.get(ret);
  }

  points_collection_t points_data;
  points_data_initialize(points_data, header);
  auto min = tree->morton_min;
  uint16_t name = morton::morton_get_name(0, 0, morton::morton_get_child_mask(morton::morton_magnitude_to_lod(tree->magnitude) + 1, points_data.min));
  assert(name == tree->node_ids[0][0]);
  tree->storage_map.add_storage(header.input_id, attributes_id, std::move(locations));
  sub_tree_insert_points(tree_registry, cache, tree->id, min, 0, 0, name, std::move(points_data));
  return ret;
}

static uint32_t points_collection_serialize_size(const points_collection_t &points)
{
  uint32_t size = 0;
  size += sizeof(points.point_count);
  size += sizeof(points.min);
  size += sizeof(points.max);
  size += sizeof(points.min_lod);
  size += sizeof(uint32_t(points.data.size()));
  size += sizeof(points.data[0]) * uint32_t(points.data.size());
  return size;
}

static uint32_t points_collections_serialize_size(const std::vector<points_collection_t> &points)
{
  uint32_t size = 0;
  size += sizeof(uint32_t); // points.size()
  for (auto &p : points)
  {
    size += points_collection_serialize_size(p);
  }
  return int(size);
}

static std::pair<bool, uint8_t *> points_collection_serialize(const points_collection_t &points, uint8_t *buffer, const uint8_t *end_ptr)
{
  uint8_t *ptr = buffer;

  if (!write_memory(ptr, end_ptr, points.point_count))
    return {false, ptr};
  if (!write_memory(ptr, end_ptr, points.min))
    return {false, ptr};
  if (!write_memory(ptr, end_ptr, points.max))
    return {false, ptr};
  if (!write_memory(ptr, end_ptr, points.min_lod))
    return {false, ptr};

  auto data_size = uint32_t(points.data.size());
  if (!write_memory(ptr, end_ptr, data_size))
    return {false, ptr};

  if (!write_vec_type(ptr, end_ptr, points.data))
    return {false, ptr};
  return {true, ptr};
}

static std::pair<bool, const uint8_t *> points_collection_deserialize(const uint8_t *buffer, const uint8_t *end_ptr, points_collection_t &points)
{
  auto ptr = buffer;
  if (!read_memory(ptr, end_ptr, points.point_count))
    return {false, ptr};
  if (!read_memory(ptr, end_ptr, points.min))
    return {false, ptr};
  if (!read_memory(ptr, end_ptr, points.max))
    return {false, ptr};
  if (!read_memory(ptr, end_ptr, points.min_lod))
    return {false, ptr};

  uint32_t data_size = 0;
  if (!read_memory(ptr, end_ptr, data_size))
    return {false, ptr};

  if (!read_vec_type(ptr, end_ptr, points.data, data_size))
    return {false, ptr};

  return {true, ptr};
}

static std::pair<bool, uint8_t *> points_collections_serialize(const std::vector<points_collection_t> &points, uint8_t *buffer, const uint8_t *end_ptr)
{
  uint8_t *ptr = buffer;

  auto size = uint32_t(points.size());
  if (!write_memory(ptr, end_ptr, size))
    return {false, ptr};

  for (auto &p : points)
  {
    auto result = points_collection_serialize(p, ptr, end_ptr);
    ptr = result.second;
    if (!result.first)
      return {false, ptr};
  }
  return {true, ptr};
}

static std::pair<bool, const uint8_t *> points_collections_deserialize(const uint8_t *start, const uint8_t *end, std::vector<points_collection_t> &points)
{
  auto ptr = start;
  uint32_t size = 0;
  if (!read_memory(ptr, end, size))
    return {false, ptr};

  points.resize(size);
  for (auto &p : points)
  {
    auto result = points_collection_deserialize(ptr, end, p);
    if (!result.first)
      return result;
    ptr = result.second;
  }
  return {true, ptr};
}

serialized_tree_t tree_serialize(const tree_t &tree)
{
  size_t tree_size = 0;
  tree_size += sizeof(tree.morton_min);
  tree_size += sizeof(tree.morton_max);
  tree_size += sizeof(tree.id);
  tree_size += sizeof(tree.magnitude);
  for (int i = 0; i < 5; i++)
  {
    assert(tree.nodes[i].size() == tree.skips[i].size());
    assert(tree.nodes[i].size() == tree.node_ids[i].size());
    assert(tree.nodes[i].size() == tree.data[i].size());
    auto level_size = uint32_t(tree.nodes[i].size());
    tree_size += sizeof(level_size);
    tree_size += level_size * sizeof(tree.nodes[i][0]);
    tree_size += level_size * sizeof(tree.skips[i][0]);
    tree_size += level_size * sizeof(tree.node_ids[i][0]);
    tree_size += points_collections_serialize_size(tree.data[i]);
  }
  tree_size += sizeof(uint32_t); // tree.sub_trees.size()
  tree_size += tree.sub_trees.size() * sizeof(tree.sub_trees[0]);
  tree_size += tree.storage_map.serialized_size();

  auto data = std::make_shared<uint8_t[]>(tree_size);
  uint8_t *ptr = data.get();
  uint8_t *end_ptr = ptr + tree_size;

  if (!write_memory(ptr, end_ptr, tree.morton_min))
    return {nullptr, 0};
  if (!write_memory(ptr, end_ptr, tree.morton_max))
    return {nullptr, 0};
  if (!write_memory(ptr, end_ptr, tree.id))
    return {nullptr, 0};
  if (!write_memory(ptr, end_ptr, tree.magnitude))
    return {nullptr, 0};

  for (int i = 0; i < 5; i++)
  {
    auto level_size = uint32_t(tree.nodes[i].size());
    if (!write_memory(ptr, end_ptr, level_size))
      return {nullptr, 0};
    if (level_size == 0)
      continue;
    if (!write_vec_type(ptr, end_ptr, tree.nodes[i]))
      return {nullptr, 0};
    if (!write_vec_type(ptr, end_ptr, tree.skips[i]))
      return {nullptr, 0};
    if (!write_vec_type(ptr, end_ptr, tree.node_ids[i]))
      return {nullptr, 0};

    auto result = points_collections_serialize(tree.data[i], ptr, end_ptr);
    ptr = result.second;
    if (!result.first)
      return {nullptr, 0};
  }

  auto sub_trees_size = uint32_t(tree.sub_trees.size());
  if (!write_memory(ptr, end_ptr, sub_trees_size))
    return {nullptr, 0};

  if (sub_trees_size > 0 && !write_vec_type(ptr, end_ptr, tree.sub_trees))
    return {nullptr, 0};

  auto result = tree.storage_map.serialize(ptr, end_ptr);
  ptr = result.second;
  if (!result.first)
    return {nullptr, 0};

  return {std::move(data), int(tree_size)};
}

bool tree_deserialize(const serialized_tree_t &serialized_tree, tree_t &tree, error_t &error)
{
  const uint8_t *ptr = serialized_tree.data.get();
  const uint8_t *end_ptr = ptr + serialized_tree.size;

  if (!read_memory(ptr, end_ptr, tree.morton_min))
  {
    error = {1, "Invalid tree data"};
    return false;
  }

  if (!read_memory(ptr, end_ptr, tree.morton_max))
  {
    error = {1, "Invalid tree data"};
    return false;
  }

  if (!read_memory(ptr, end_ptr, tree.id))
  {
    error = {1, "Invalid tree data"};
    return false;
  }

  if (!read_memory(ptr, end_ptr, tree.magnitude))
  {
    error = {1, "Invalid tree data"};
    return false;
  }

  for (int i = 0; i < 5; i++)
  {
    uint32_t level_size = 0;
    if (!read_memory(ptr, end_ptr, level_size))
    {
      error = {1, "Invalid tree data"};
      return false;
    }
    if (level_size == 0)
      continue;
    if (!read_vec_type(ptr, end_ptr, tree.nodes[i], level_size))
    {
      error = {1, "Invalid tree data"};
      return false;
    }

    if (!read_vec_type(ptr, end_ptr, tree.skips[i], level_size))
    {
      error = {1, "Invalid tree data"};
      return false;
    }

    if (!read_vec_type(ptr, end_ptr, tree.node_ids[i], level_size))
    {
      error = {1, "Invalid tree data"};
      return false;
    }

    auto result = points_collections_deserialize(ptr, end_ptr, tree.data[i]);
    ptr = result.second;
    if (!result.first)
    {
      error = {1, "Invalid tree data"};
      return false;
    }
  }

  uint32_t sub_trees_size = 0;
  if (!read_memory(ptr, end_ptr, sub_trees_size))
  {
    error = {1, "Invalid tree data"};
    return false;
  }

  if (sub_trees_size > 0 && !read_vec_type(ptr, end_ptr, tree.sub_trees, sub_trees_size))
  {
    error = {1, "Invalid tree data"};
    return false;
  }

  auto result = tree.storage_map.deserialize(ptr, end_ptr);
  ptr = result.second;
  if (!result.first)
  {
    error = {1, "Invalid tree data"};
    return false;
  }

  return true;
}

serialized_tree_registry_t tree_registry_serialize(const tree_registry_t &tree_registry)
{
  uint32_t tree_registry_size = 0;
  tree_registry_size += sizeof(tree_registry.node_limit);
  tree_registry_size += sizeof(tree_registry.current_id);
  tree_registry_size += sizeof(tree_registry.tree_config);
  auto tree_registry_count = uint32_t(tree_registry.locations.size());
  tree_registry_size += sizeof(tree_registry_count);
  tree_registry_size += sizeof(storage_location_t) * tree_registry_count;

  auto data = std::make_shared<uint8_t[]>(tree_registry_size);
  uint8_t *ptr = data.get();
  uint8_t *end_ptr = ptr + tree_registry_size;

  if (!write_memory(ptr, end_ptr, tree_registry.node_limit))
    return {nullptr, 0};
  if (!write_memory(ptr, end_ptr, tree_registry.current_id))
    return {nullptr, 0};
  if (!write_memory(ptr, end_ptr, tree_registry.tree_config))
    return {nullptr, 0};
  if (!write_memory(ptr, end_ptr, tree_registry_count))
    return {nullptr, 0};
  if (!write_vec_type(ptr, end_ptr, tree_registry.locations))
    return {nullptr, 0};
  return {std::move(data), int(tree_registry_size)};
}

error_t tree_registry_deserialize(const std::unique_ptr<uint8_t[]> &data, uint32_t data_size, tree_registry_t &tree_registry)
{
  const uint8_t *ptr = data.get();
  const uint8_t *end_ptr = ptr + data_size;
  if (!read_memory(ptr, end_ptr, tree_registry.node_limit))
    return {1, "Invalid tree registry data"};
  if (!read_memory(ptr, end_ptr, tree_registry.current_id))
    return {1, "Invalid tree registry data"};
  if (!read_memory(ptr, end_ptr, tree_registry.tree_config))
    return {1, "Invalid tree registry data"};
  uint32_t tree_registry_count = 0;
  if (!read_memory(ptr, end_ptr, tree_registry_count))
    return {1, "Invalid tree registry data"};
  if (!read_vec_type(ptr, end_ptr, tree_registry.locations, tree_registry_count))
    return {1, "Invalid tree registry data"};

  tree_registry.data.resize(tree_registry_count);
  tree_registry.tree_id_initialized.resize(tree_registry_count);
  return {};
}

} // namespace points::converter
