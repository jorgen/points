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
#include "frame_node_registry.hpp"
#include "gpu_node_buffer.hpp"

namespace points::converter
{

registry_diff_t frame_node_registry_t::update_from_walker(const std::vector<tree_walker_data_t> &point_subsets,
                                                          const std::vector<std::pair<node_id_t, node_id_t>> &parent_child_edges,
                                                          const std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers)
{
  registry_diff_t diff;

  // Build the new set of node_ids from walker output
  node_set_t new_node_ids;
  for (auto &ps : point_subsets)
    new_node_ids.insert(ps.node);

  // Find removed nodes
  for (auto &[id, node] : m_nodes)
  {
    if (!new_node_ids.count(id))
      diff.removed.push_back(id);
  }

  // Remove them
  for (auto &id : diff.removed)
    m_nodes.erase(id);

  // Update or add nodes from point_subsets
  for (int idx = 0; idx < int(point_subsets.size()); idx++)
  {
    auto &ps = point_subsets[idx];
    auto it = m_nodes.find(ps.node);
    if (it == m_nodes.end())
    {
      // New node
      auto &node = m_nodes[ps.node];
      node.id = ps.node;
      node.parent_id = ps.parent;
      node.aabb = ps.aabb;
      node.tight_aabb = ps.tight_aabb;
      node.lod = ps.lod;
      node.frustum_visible = ps.frustum_visible;
      node.total_point_count = ps.point_count.data;
      node.buffer_indices.push_back(idx);
      diff.added.push_back(ps.node);
    }
    else
    {
      // Existing node — update mutable fields
      auto &node = it->second;
      node.frustum_visible = node.frustum_visible || ps.frustum_visible;
      node.total_point_count += ps.point_count.data;
      node.buffer_indices.push_back(idx);
      // Only add to updated on first buffer of this node
      if (node.buffer_indices.size() == 1)
        diff.updated.push_back(ps.node);
    }
  }

  // Deduplicate added (multiple subsets for same node)
  {
    node_set_t seen;
    std::vector<node_id_t> unique_added;
    for (auto &id : diff.added)
    {
      if (seen.insert(id).second)
        unique_added.push_back(id);
    }
    diff.added = std::move(unique_added);
  }

  // Reset per-frame fields before recomputing
  for (auto &[id, node] : m_nodes)
  {
    node.buffer_indices.clear();
    node.total_point_count = 0;
    node.frustum_visible = false;
    node.children.clear();
    node.all_rendered = true;
    node.all_fade_complete = true;
    node.gpu_memory_size = 0;
  }

  // Recompute buffer_indices and aggregate state from render_buffers
  for (int idx = 0; idx < int(render_buffers.size()); idx++)
  {
    auto &rb = *render_buffers[idx];
    auto it = m_nodes.find(rb.node_info.node);
    if (it == m_nodes.end())
      continue;
    auto &node = it->second;
    node.buffer_indices.push_back(idx);
    node.frustum_visible = node.frustum_visible || rb.node_info.frustum_visible;
    node.total_point_count += rb.node_info.point_count.data;
    node.parent_id = rb.node_info.parent;
    node.aabb = rb.node_info.aabb;
    node.tight_aabb = rb.node_info.tight_aabb;
    node.lod = rb.node_info.lod;
    if (!rb.rendered)
      node.all_rendered = false;
    if (!rb.rendered || rb.fade_frame < gpu_node_buffer_t::FADE_FRAMES || rb.old_color_valid || rb.awaiting_new_color)
      node.all_fade_complete = false;
    node.gpu_memory_size += rb.gpu_memory_size;
  }

  // Build children from edges
  node_set_t all_node_ids;
  for (auto &[id, node] : m_nodes)
    all_node_ids.insert(id);

  for (auto &[parent_id, child_id] : parent_child_edges)
  {
    if (all_node_ids.count(parent_id) && all_node_ids.count(child_id))
      m_nodes[parent_id].children.push_back(child_id);
  }

  // Deduplicate children
  for (auto &[id, node] : m_nodes)
  {
    node_set_t child_set;
    std::vector<node_id_t> unique_children;
    for (auto &child : node.children)
    {
      if (child_set.insert(child).second)
        unique_children.push_back(child);
    }
    node.children = std::move(unique_children);
  }

  // Find roots: nodes whose parent is not in the registry
  m_roots.clear();
  for (auto &[id, node] : m_nodes)
  {
    if (!m_nodes.count(node.parent_id))
      m_roots.push_back(id);
  }

  return diff;
}

const registry_node_t *frame_node_registry_t::get_node(const node_id_t &id) const
{
  auto it = m_nodes.find(id);
  return it != m_nodes.end() ? &it->second : nullptr;
}

registry_node_t *frame_node_registry_t::get_node_mut(const node_id_t &id)
{
  auto it = m_nodes.find(id);
  return it != m_nodes.end() ? &it->second : nullptr;
}

} // namespace points::converter
