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

#include "frame_node_registry.hpp"
#include <glm_include.hpp>

namespace points::converter
{

struct selection_params_t
{
  glm::dvec3 camera_position;
  size_t memory_budget = 512 * 1024 * 1024;
  uint64_t point_budget = 10'000'000;
};

struct selection_result_t
{
  frame_node_registry_t::node_set_t active_set;
  uint64_t total_points = 0;
  size_t total_memory = 0;
  std::vector<node_id_t> frontier_nodes;
};

class node_selector_t
{
public:
  selection_result_t select(const frame_node_registry_t &registry, const selection_params_t &params, bool debug = false,
                            bool any_transitioning = false) const;
};

} // namespace points::converter
