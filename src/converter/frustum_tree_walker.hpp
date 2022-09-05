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
#pragma once

#include <tree_handler.hpp>
#include <points/render/aabb.h>

#include <glm_include.hpp>

namespace points
{
namespace converter
{

struct tree_walker_data_t
{
  std::vector<points_subset_t> data;
};

struct tree_walker_nodes_t
{
  morton::morton192_t min_morton;
  tree_id_t id;
  uint16_t level;
  std::vector<uint16_t> morton_nodes[5];
  std::vector<tree_walker_data_t> point_subsets[5];
};

class frustum_tree_walker_t
{
public:
  frustum_tree_walker_t(glm::dmat4 view_perspective);
  void walk_tree(const tree_global_state_t &global_state, tree_cache_t tree_cache, tree_id_t tree_root);

  bool done();

  glm::dmat4 m_view_perspective;
  render::aabb_t m_tree_aabb;
  std::mutex m_mutex;
  tree_walker_nodes_t m_new_nodes;
  bool m_done;
};

}
}
