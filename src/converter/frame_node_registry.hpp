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

#include "frustum_tree_walker.hpp"

#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace points::converter
{

struct node_id_hash
{
  size_t operator()(const node_id_t &id) const
  {
    uint64_t v;
    memcpy(&v, &id, sizeof(v));
    return std::hash<uint64_t>()(v);
  }
};

struct node_id_equal
{
  bool operator()(const node_id_t &a, const node_id_t &b) const
  {
    return (a <=> b) == std::strong_ordering::equal;
  }
};

enum class coverage_state_t
{
  self_covering,
  children_covering,
  mixed_covering,
  inactive
};

struct registry_node_t
{
  node_id_t id = {};
  node_id_t parent_id = {};
  node_aabb_t aabb = {};
  node_aabb_t tight_aabb = {};
  int lod = 0;
  bool frustum_visible = true;
  uint64_t total_point_count = 0;
  size_t gpu_memory_size = 0;
  coverage_state_t coverage = coverage_state_t::inactive;
  bool all_rendered = false;
  bool all_fade_complete = false;
  std::vector<node_id_t> children;
  std::vector<int> buffer_indices;
};

struct registry_diff_t
{
  std::vector<node_id_t> added;
  std::vector<node_id_t> removed;
  std::vector<node_id_t> updated;
};

class frame_node_registry_t
{
public:
  using node_map_t = std::unordered_map<node_id_t, registry_node_t, node_id_hash, node_id_equal>;
  using node_set_t = std::unordered_set<node_id_t, node_id_hash, node_id_equal>;

  registry_diff_t update_from_walker(const std::vector<tree_walker_data_t> &point_subsets,
                                     const std::vector<std::pair<node_id_t, node_id_t>> &parent_child_edges,
                                     const std::vector<std::unique_ptr<struct gpu_node_buffer_t>> &render_buffers,
                                     const node_set_t &fade_out_retain = {});

  const registry_node_t *get_node(const node_id_t &id) const;
  registry_node_t *get_node_mut(const node_id_t &id);
  const node_map_t &nodes() const { return m_nodes; }
  const std::vector<node_id_t> &roots() const { return m_roots; }
  bool empty() const { return m_nodes.empty(); }

private:
  node_map_t m_nodes;
  std::vector<node_id_t> m_roots;
};

} // namespace points::converter
