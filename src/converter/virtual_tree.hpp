/************************************************************************
** dewfall - point cloud management software.
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

#include "conversion_types.hpp" // tree_config_t
#include "frustum_tree_walker.hpp" // lod_params_t
#include "render_pipeline.hpp"     // render_list_t, render::callback_manager_t
#include "resident_source.hpp"
#include "virtual_node.hpp"

#include <frustum.hpp> // render::frustum_t
#include <vio/thread_pool.h>

#include <memory>
#include <vector>

struct dew_to_render_t;

namespace dew::converter
{

// Everything the per-frame virtual passes need, bundled to keep signatures small.
struct virtual_frame_t
{
  const render::frame_camera_cpp_t *camera = nullptr;
  glm::dvec3 camera_position = {};
  const tree_config_t *tree_config = nullptr;
  const lod_params_t *lod_params = nullptr;
  render::callback_manager_t *callbacks = nullptr;
  vio::thread_pool_t *convert_pool = nullptr;
  const std::vector<float> *lod_random_offsets = nullptr;
  size_t gpu_memory_budget = 0;
  size_t *gpu_memory_used = nullptr;    // running total of virtual-node GPU bytes (shared across frames)
  size_t real_gpu_used = 0;             // monolith GPU bytes this frame -> the shared budget admission gate
  size_t own_monolith_bytes = 0;        // the current source leaf's still-resident monolith (reclaimable) -> not
                                        //   counted against its own cut, else the cut can never grow (deadlock)
  uint32_t virtual_min_points = 1;
  uint32_t frame_index = 0;             // monotonic per-frame counter (selection + TTL eviction age)
  uint32_t evict_ttl_frames = 30;       // hold an unselected virtual node this many frames before freeing it
  uint32_t prune_ttl_frames = 120;      // reclaim a long-cold node's child struct subtree (rebuilt on re-descent)
  uint32_t monolith_free_after_frames = 30; // free a promoted leaf's monolith after the cut is stably complete
  float delta_ms = 16.0f;               // frame time (virtual fade advance)
  float fade_duration_ms = 300.0f;
  int viewport_height = 1080;           // per-point LOD submit bound (same as the real emit)
  int subdivide_floor_lod = 9;          // don't subdivide a virtual node finer than THIS frame's finest real
                                        //   render node (also caps recursion so a dense cluster can't recurse
                                        //   past level 0 into negative morton levels -> abort)
  double render_density_px = 1.0;       // Density (px) slider -> uniform on-screen point spacing
  double attr_min = 0.0;                // attribute contrast-stretch range (intensity normalization)
  double attr_max = 1.0;
  bool any_animating = false;           // OUT: a virtual node is materializing or fading -> keep requesting frames
};

// Decode a loaded spanning leaf into a resident source: keep its morton-sorted codes + one morton-order r32x3
// decode so virtual nodes can slice/gather it without re-reading storage. node_min/leaf_lod are set from the
// header's tight span (the same cell the decode uses), so the octant split stays consistent with the decode.
std::shared_ptr<resident_source_t> build_resident_source(std::shared_ptr<dyn_points_data_handler_t> data_handler, const tree_config_t &tree_config);

// Wrap a whole resident leaf as the root of its virtual octree (first_index=0, src_count=point_count,
// level=leaf_lod, octant_min=node_min). tight/loose come from the promoted leaf's walker aabbs. draw_type is
// inferred from the color attribute's component count.
std::unique_ptr<virtual_node_t> make_virtual_root(const resident_source_t &src, const node_aabb_t &tight_aabb, const node_aabb_t &loose_aabb);

// Build `node`'s up to 8 octant children (index windows + tight/loose bboxes + level = node.level-1). Cheap
// index math over the immutable resident codes; cached via node.children_built. No-op if already built.
void split_octants(virtual_node_t &node, const resident_source_t &src, const tree_config_t &tree_config);

// Produce a virtual node's OWNED drawable buffers (node.vertex_data / attribute_data / draw_count) at its
// level's LOD: the SHARED quantizer picks one representative per maskWidth=max(0,node.level-9) morton cell and
// gathers them, so a virtual LOD node is byte-generated by the exact scheme a stored LOD node is. maskWidth==0
// (finest) draws every point. Safe on a worker thread (reads only the immutable resident source + node.level).
void materialize_virtual_node(virtual_node_t &node, const resident_source_t &src, const std::vector<float> &lod_random_offsets);

// Per-frame: for each is_virtual_source render node, walk virtual_root (frustum + should_subdivide +
// nearest-distance), materialize newly-selected nodes on the convert_pool, upload the ready ones under the
// shared GPU budget, and evict the unselected. Keeps render_node_t::draw_suppressed set while promoted (a
// promoted leaf's own full-res monolith is never drawn; ancestors + the additive cut carry the region).
void process_virtual_trees(render_list_t &render_list, virtual_frame_t &frame);

// Emit flat draw groups (lod_density_scale=0 -> the shader keeps all points) for selected+uploaded virtual
// nodes, refreshing each node's camera uniform. Returns the number of virtual nodes drawn.
int emit_virtual_draws(render_list_t &render_list, render::callback_manager_t &callbacks, const render::frame_camera_cpp_t &camera, const tree_config_t &tree_config, dew_to_render_t *to_render, uint32_t frame_index, int viewport_height, double render_density_px, float fade_duration_ms, uint64_t &points_rendered);

// Destroy a virtual subtree children-first, spin-waiting each in-flight materialize before freeing.
void destroy_virtual_subtree(std::unique_ptr<virtual_node_t> &root, render::callback_manager_t &callbacks, size_t *gpu_memory_used);

// True if any node in the subtree still has a materialize job in flight on the convert pool. Used by the
// render pipeline to DEFER (rather than block on) destroying a departed node whose virtual cut is still
// decoding -- destroy_virtual_subtree would otherwise spin-wait on the main thread.
bool virtual_subtree_has_inflight(const virtual_node_t &root);

} // namespace dew::converter
