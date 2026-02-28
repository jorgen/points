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
#include "node_selector.hpp"
#include <algorithm>
#include <fmt/printf.h>

namespace points::converter
{

selection_result_t node_selector_t::select(const frame_node_registry_t &registry, const selection_params_t &params, bool debug,
                                            bool any_transitioning) const
{
  selection_result_t result;

  if (registry.empty())
    return result;

  // Start with root nodes
  for (auto &root_id : registry.roots())
  {
    result.active_set.insert(root_id);
    auto *root = registry.get_node(root_id);
    if (root && root->all_rendered)
    {
      result.total_memory += root->gpu_memory_size;
      result.total_points += root->total_point_count;
    }
  }

  // Iteratively expand active nodes into children, closest-first
  struct expansion_t
  {
    node_id_t node_id;
    double distance;
  };
  std::vector<expansion_t> candidates;
  std::vector<node_id_t> to_remove;
  std::vector<node_id_t> to_add;
  std::vector<node_id_t> rendered_children;
  std::vector<node_id_t> new_children;

  constexpr int MAX_EXPANSION_ITERATIONS = 8;
  bool changed = true;
  int iterations = 0;
  while (changed && iterations < MAX_EXPANSION_ITERATIONS)
  {
    changed = false;
    iterations++;

    candidates.clear();
    for (auto &node_id : result.active_set)
    {
      auto *node = registry.get_node(node_id);
      if (!node || node->children.empty())
        continue;
      glm::dvec3 center = (node->tight_aabb.min + node->tight_aabb.max) * 0.5;
      double dist = glm::length(center - params.camera_position);
      candidates.push_back({node_id, dist});
    }
    std::sort(candidates.begin(), candidates.end(), [](const expansion_t &a, const expansion_t &b) { return a.distance < b.distance; });

    to_remove.clear();
    to_add.clear();
    for (auto &candidate : candidates)
    {
      auto *parent_node = registry.get_node(candidate.node_id);
      if (!parent_node)
        continue;

      bool all_children_rendered = true;
      bool all_children_faded = true;
      size_t all_children_memory = 0;
      uint64_t all_children_points = 0;
      rendered_children.clear();
      for (auto &child_id : parent_node->children)
      {
        auto *child = registry.get_node(child_id);
        if (!child)
        {
          all_children_rendered = false;
          all_children_faded = false;
          continue;
        }
        if (child->all_rendered)
        {
          rendered_children.push_back(child_id);
          all_children_memory += child->gpu_memory_size;
          all_children_points += child->total_point_count;
          if (!child->all_fade_complete)
            all_children_faded = false;
        }
        else
        {
          all_children_rendered = false;
          all_children_faded = false;
        }
      }
      if (rendered_children.empty())
        continue;

      if (all_children_rendered && all_children_faded)
      {
        // Clean swap: remove parent, add all children
        size_t parent_memory = parent_node->gpu_memory_size;
        uint64_t parent_points = parent_node->total_point_count;
        size_t new_total_memory = result.total_memory - parent_memory + all_children_memory;
        uint64_t new_total_points = result.total_points - parent_points + all_children_points;
        if (new_total_memory > params.memory_budget && !any_transitioning)
        {
          if (debug)
            fmt::print(stderr, "[transition-debug] selector: rejected clean swap of node lod={} "
                       "(memory: {} - {} + {} = {} > budget {})\n",
                       parent_node->lod, result.total_memory, parent_memory, all_children_memory,
                       new_total_memory, params.memory_budget);
          continue;
        }
        if (new_total_points > params.point_budget)
        {
          if (debug)
            fmt::print(stderr, "[transition-debug] selector: rejected clean swap of node lod={} "
                       "(points: {} > budget {})\n",
                       parent_node->lod, new_total_points, params.point_budget);
          continue;
        }
        to_remove.push_back(candidate.node_id);
        for (auto &child_id : rendered_children)
          to_add.push_back(child_id);
        result.total_memory = new_total_memory;
        result.total_points = new_total_points;
        changed = true;
      }
      else
      {
        // Partial: keep parent, add rendered children alongside
        size_t new_children_memory = 0;
        uint64_t new_children_points = 0;
        new_children.clear();
        for (auto &child_id : rendered_children)
        {
          if (!result.active_set.count(child_id))
          {
            auto *child = registry.get_node(child_id);
            if (child)
            {
              new_children.push_back(child_id);
              new_children_memory += child->gpu_memory_size;
              new_children_points += child->total_point_count;
            }
          }
        }
        if (new_children.empty())
          continue;
        if (result.total_memory + new_children_memory > params.memory_budget && !any_transitioning)
        {
          if (debug)
            fmt::print(stderr, "[transition-debug] selector: rejected partial expansion of node lod={} "
                       "(memory: {} + {} > budget {})\n",
                       parent_node->lod, result.total_memory, new_children_memory, params.memory_budget);
          continue;
        }
        if (result.total_points + new_children_points > params.point_budget)
        {
          if (debug)
            fmt::print(stderr, "[transition-debug] selector: rejected partial expansion of node lod={} "
                       "(points: {} + {} > budget {})\n",
                       parent_node->lod, result.total_points, new_children_points, params.point_budget);
          continue;
        }
        for (auto &child_id : new_children)
          to_add.push_back(child_id);
        result.total_memory += new_children_memory;
        result.total_points += new_children_points;
        changed = true;
      }
    }
    for (auto &id : to_remove)
      result.active_set.erase(id);
    for (auto &id : to_add)
      result.active_set.insert(id);
  }

  // Compute frontier: active nodes + one-level children that need loading
  for (auto &node_id : result.active_set)
  {
    auto *node = registry.get_node(node_id);
    if (!node)
      continue;
    if (!node->all_rendered)
      result.frontier_nodes.push_back(node_id);
    for (auto &child_id : node->children)
    {
      auto *child = registry.get_node(child_id);
      if (child && !child->all_rendered)
        result.frontier_nodes.push_back(child_id);
    }
  }

  return result;
}

} // namespace points::converter
