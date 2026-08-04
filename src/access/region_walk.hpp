/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#pragma once

// Octree descent for QUERIES: select the nodes overlapping a world-space box, at a chosen level of
// detail, and report which sub-trees still have to be loaded before the answer is complete.
//
// This is the query counterpart of the renderer's frustum_tree_walker. It is a separate descent on
// purpose: the renderer selects by screen-space error and wants every level it passes through (it
// crossfades between them), whereas a query wants a single, non-overlapping set of nodes.
//
//   *** LOD nodes are SUBSAMPLED COPIES of their descendants, not a partition. ***
//
// tree_lod_generator samples bottom-up, so an interior node holds a thinned copy of everything below
// it. Emitting a node AND its children double-counts -- by 2-3x on a real dataset, while still
// looking entirely plausible. Every mode below therefore emits exactly one frontier: the set of
// nodes at which the descent stopped, and never an ancestor of another emitted node.

#include "dataset_types.hpp"
#include "input_storage_map.hpp"
#include "tree.hpp"

#include <cstdint>
#include <vector>

namespace dew::access
{
using namespace dew::core;

// Axis-aligned box in world units. Plain doubles -- the access module carries no glm dependency.
struct aabb_t
{
  double min[3];
  double max[3];
};

inline bool aabb_overlaps(const aabb_t &a, const aabb_t &b)
{
  for (int i = 0; i < 3; i++)
  {
    if (a.max[i] < b.min[i] || a.min[i] > b.max[i])
      return false;
  }
  return true;
}

inline bool aabb_contains(const aabb_t &outer, const aabb_t &inner)
{
  for (int i = 0; i < 3; i++)
  {
    if (inner.min[i] < outer.min[i] || inner.max[i] > outer.max[i])
      return false;
  }
  return true;
}

// Halve `parent` into octant `child_index` (bit i selects the high half on axis i). Mirrors the
// renderer's make_aabb_from_child_index so a query cell and a rendered cell agree exactly.
aabb_t aabb_child(const aabb_t &parent, int child_index);

enum class lod_mode_t
{
  full,         // descend to the leaves; emit leaf data only (full resolution)
  level,        // stop at a given morton lod and emit that frontier
  point_budget, // descend while the running point total stays under a budget
};

struct region_query_t
{
  aabb_t box{{0, 0, 0}, {0, 0, 0}};
  lod_mode_t lod_mode = lod_mode_t::full;
  int32_t lod = 0;             // lod_mode::level
  uint64_t max_points = 0;     // lod_mode::point_budget
  bool whole_dataset = false;  // ignore `box` and take everything
};

// One selected node's readable unit: which storage-map entry to read, how many points, and where it
// sits in the world.
struct region_node_t
{
  tree_id_t tree_id;
  uint16_t level = 0;
  uint16_t index = 0;
  int32_t lod = 0;
  input_data_id_t input_id{};
  attributes_id_t attributes_id{};
  offset_in_subset_t offset_in_subset{};
  point_count_t point_count{};
  aabb_t cell{{0, 0, 0}, {0, 0, 0}};   // the loose octree cube
  aabb_t tight{{0, 0, 0}, {0, 0, 0}};  // the actual bounds of the points in it
  uint8_t child_mask = 0;
  bool is_leaf = false;
  bool is_lod = false;
  // True when the node's cell lies wholly inside the query box, so per-point clipping can be skipped.
  bool fully_inside = false;
};

struct region_result_t
{
  std::vector<region_node_t> nodes;
  // Sub-trees the descent needed but that are not resident yet. The caller loads these and walks
  // again; the walk is otherwise complete when this is empty.
  std::vector<tree_id_t> trees_to_load;
  uint64_t total_points = 0;
};

// Walk `registry` (whatever of it is resident) and select the nodes matching `query`. Pure and
// synchronous: it never reads storage, so it can run on the caller's thread. Re-run it after loading
// `trees_to_load` until that list comes back empty.
void region_walk(const tree_registry_t &registry, const region_query_t &query, region_result_t &out);

} // namespace dew::access
