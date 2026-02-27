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

  // Build the new set of node_ids from walker output using try_emplace
  // to avoid a separate seen set for dedup
  node_set_t new_node_ids;
  for (auto &ps : point_subsets)
  {
    new_node_ids.insert(ps.node);
    auto [it, inserted] = m_nodes.try_emplace(ps.node);
    if (inserted)
    {
      auto &node = it->second;
      node.id = ps.node;
      node.parent_id = ps.parent;
      node.aabb = ps.aabb;
      node.tight_aabb = ps.tight_aabb;
      node.lod = ps.lod;
      diff.added.push_back(ps.node);
    }
  }

  // Find and remove nodes no longer in walker output
  for (auto it = m_nodes.begin(); it != m_nodes.end();)
  {
    if (!new_node_ids.count(it->first))
    {
      diff.removed.push_back(it->first);
      it = m_nodes.erase(it);
    }
    else
    {
      ++it;
    }
  }

  // Reset per-frame fields before recomputing from render_buffers
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

  // Compute buffer_indices and aggregate state from render_buffers
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
    if (!rb.rendered || (rb.node_info.frustum_visible && rb.fade_frame < gpu_node_buffer_t::FADE_FRAMES))
      node.all_fade_complete = false;
    node.gpu_memory_size += rb.gpu_memory_size;
  }

  // Build children from edges (use m_nodes.find directly, no separate set)
  for (auto &[parent_id, child_id] : parent_child_edges)
  {
    auto parent_it = m_nodes.find(parent_id);
    if (parent_it == m_nodes.end())
      continue;
    if (m_nodes.find(child_id) == m_nodes.end())
      continue;
    // Linear scan dedup (max 8 children per octree node)
    auto &children = parent_it->second.children;
    bool already_present = false;
    for (auto &c : children)
    {
      if ((c <=> child_id) == std::strong_ordering::equal)
      {
        already_present = true;
        break;
      }
    }
    if (!already_present)
      children.push_back(child_id);
  }

  // Find roots: nodes whose parent is not in the registry
  m_roots.clear();
  for (auto &[id, node] : m_nodes)
  {
    if (m_nodes.find(node.parent_id) == m_nodes.end())
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
