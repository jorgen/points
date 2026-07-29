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
#include "virtual_tree.hpp"

#include "frustum_tree_walker.hpp"             // make_aabb_from_child_index
#include "lod_quantize.hpp"                     // find_indices_to_quantize, morton_to_lod_t
#include "morton.hpp"
#include "morton_tree_coordinate_transform.hpp" // convert_morton_to_pos
#include "point_buffer_render_helper.hpp"       // convert_points_to_vertex_data, dyn_points_draw_buffer_t
#include "point_buffer_splitter.hpp"            // for_each_octant_range
#include "renderer.hpp"                         // points_to_render_add_render_group

#include <data_source.hpp> // render::frame_camera_cpp_t

#include <thread>

#include <algorithm>
#include <cstring>

namespace points::converter
{

static uint32_t morton_type_size(points_type_t t)
{
  switch (t)
  {
  case points_type_m32:
    return uint32_t(sizeof(morton::morton32_t));
  case points_type_m64:
    return uint32_t(sizeof(morton::morton64_t));
  case points_type_m128:
    return uint32_t(sizeof(morton::morton128_t));
  case points_type_m192:
    return uint32_t(sizeof(morton::morton192_t));
  default:
    return 0;
  }
}

std::shared_ptr<resident_source_t> build_resident_source(std::shared_ptr<dyn_points_data_handler_t> data_handler, const tree_config_t &tree_config)
{
  auto src = std::make_shared<resident_source_t>();
  src->data_handler = data_handler;
  src->morton_type = data_handler->header.point_format.type;
  src->point_count = data_handler->header.point_count;
  // The decode (convert_points_to_vertex_data_morton) treats the points as living in a cell at header.lod_span;
  // use the SAME cell as the split origin so octant ranges and decoded positions stay consistent.
  const int lod_span = int(data_handler->header.lod_span);
  src->leaf_lod = lod_span;
  src->node_min = morton::morton_and(data_handler->header.morton_min, morton::morton_negate(morton::morton_mask_create<uint64_t, 3>(lod_span)));

  // One morton-order r32x3 decode of the whole leaf (NOT the reordered upload buffer).
  dyn_points_draw_buffer_t tmp;
  tmp.point_count = src->point_count;
  tmp.data_handler = data_handler;
  convert_points_to_vertex_data(tree_config, *data_handler, tmp);
  src->decoded_vertex = tmp.data[0];
  src->decode_offset = tmp.offset;

  size_t bytes = data_handler->data_info[0].size + size_t(src->point_count) * 3u * sizeof(float);
  if (data_handler->data_info[1].data)
    bytes += data_handler->data_info[1].size;
  src->cpu_bytes = bytes;
  return src;
}

template <typename T, size_t C>
static void split_octants_typed(virtual_node_t &node, const resident_source_t &src, const tree_config_t &tree_config)
{
  const auto *codes = static_cast<const morton::morton_t<T, C> *>(src.data_handler->data_info[0].data);
  const auto *begin = codes + node.first_index;
  const auto *end = begin + node.src_count;
  for_each_octant_range<T, C>(begin, end, node.level, node.octant_min,
                             [&](int i, const morton::morton_t<T, C> *range_begin, size_t count, const morton::morton192_t &global_first, const morton::morton192_t &global_last) {
                               auto child = std::make_unique<virtual_node_t>();
                               child->first_index = uint32_t(range_begin - codes);
                               child->src_count = uint32_t(count);
                               child->level = node.level - 1;
                               child->octant_min = node.octant_min;
                               morton::morton_set_child_mask(node.level, uint8_t(i), child->octant_min);
                               double mn[3], mx[3];
                               convert_morton_to_pos(tree_config.scale, tree_config.offset, global_first, mn);
                               convert_morton_to_pos(tree_config.scale, tree_config.offset, global_last, mx);
                               child->tight_aabb = {glm::dvec3(mn[0], mn[1], mn[2]), glm::dvec3(mx[0], mx[1], mx[2])};
                               child->loose_aabb = make_aabb_from_child_index(node.loose_aabb, i);
                               child->draw_type = node.draw_type;
                               node.children[size_t(i)] = std::move(child);
                             });
}

std::unique_ptr<virtual_node_t> make_virtual_root(const resident_source_t &src, const node_aabb_t &tight_aabb, const node_aabb_t &loose_aabb)
{
  auto root = std::make_unique<virtual_node_t>();
  root->first_index = 0;
  root->src_count = src.point_count;
  root->level = src.leaf_lod;
  root->octant_min = src.node_min;
  root->tight_aabb = tight_aabb;
  root->loose_aabb = loose_aabb;
  const bool rgb = src.data_handler->point_format[1].components == points_components_3;
  root->draw_type = rgb ? points_dyn_points_3 : points_dyn_points_1;
  return root;
}

void split_octants(virtual_node_t &node, const resident_source_t &src, const tree_config_t &tree_config)
{
  if (node.children_built)
    return;
  if (node.level <= 0)
    return; // defensive: children would be at a negative morton level (out-of-bounds child-mask writes)
  switch (src.morton_type)
  {
  case points_type_m32:
    split_octants_typed<morton::morton32_t::component_type, morton::morton32_t::component_count::value>(node, src, tree_config);
    break;
  case points_type_m64:
    split_octants_typed<morton::morton64_t::component_type, morton::morton64_t::component_count::value>(node, src, tree_config);
    break;
  case points_type_m128:
    split_octants_typed<morton::morton128_t::component_type, morton::morton128_t::component_count::value>(node, src, tree_config);
    break;
  case points_type_m192:
    split_octants_typed<morton::morton192_t::component_type, morton::morton192_t::component_count::value>(node, src, tree_config);
    break;
  default:
    break;
  }
  node.children_built = true;
}

// Produce a virtual node's drawable buffers, reordered coarse->fine with a per-point rep_level + prefix_count,
// exactly like a stored node (build_lod_order): the node's points are one representative per maskWidth cell
// (interior) or every point (maskWidth==0, the finest), then LOD-ordered so the shader's per-point density cull
// + node crossfade work identically to the real path. Runs on a convert_pool worker (reads only immutable src).
template <typename T, size_t C>
static void materialize_typed(virtual_node_t &node, const resident_source_t &src, const std::vector<float> &offs)
{
  using MT = morton::morton_t<T, C>;
  const int maskWidth = lod_quantize_mask_width(node.level);
  const MT *window = reinterpret_cast<const MT *>(src.data_handler->data_info[0].data) + node.first_index;

  // 1) Window-local indices of the drawn points (morton-sorted) + their native-frame mortons for the ordering.
  std::vector<uint32_t> rep_local;
  std::vector<MT> rep_mortons;
  if (maskWidth == 0)
  {
    rep_local.resize(node.src_count);
    rep_mortons.resize(node.src_count);
    for (uint32_t i = 0; i < node.src_count; i++)
    {
      rep_local[i] = i;
      rep_mortons[i] = window[i];
    }
  }
  else
  {
    const uint32_t code_size = morton_type_size(src.morton_type);
    points_converter_buffer_t source_buf(static_cast<uint8_t *>(src.data_handler->data_info[0].data) + size_t(node.first_index) * code_size, node.src_count * code_size);
    using M192 = morton::morton192_t;
    std::vector<morton_to_lod_t<M192::component_type, M192::component_count::value>> reps;
    input_data_id_t dummy_id{0, 0};
    find_indices_to_quantize(dummy_id, node.octant_min, src.morton_type, source_buf, offset_in_subset_t(node.first_index), point_count_t(node.src_count), maskWidth, offs, reps);
    const uint32_t m = uint32_t(reps.size());
    rep_local.resize(m);
    rep_mortons.resize(m);
    for (uint32_t k = 0; k < m; k++)
    {
      const uint32_t local = uint32_t(reps[k].index.data) - node.first_index;
      rep_local[k] = local;
      rep_mortons[k] = window[local];
    }
  }

  // 2) Coarse->fine LOD order over the reps (shared scheme -> rep_level + prefix identical to a stored node).
  std::array<uint32_t, 64> prefix_count;
  std::vector<uint32_t> perm;
  std::vector<uint8_t> rep_level;
  build_lod_order_from_mortons<MT>(rep_mortons.data(), uint32_t(rep_mortons.size()), prefix_count, perm, rep_level);

  // 3) Gather the reordered vertex / attribute / rep_level buffers.
  const uint32_t n = uint32_t(rep_local.size());
  const uint32_t vstride = 3u * uint32_t(sizeof(float));
  const uint8_t *dv = src.decoded_vertex.get();
  const auto &ainfo = src.data_handler->data_info[1];
  const bool has_attr = ainfo.data && ainfo.size && src.point_count;
  const uint32_t astride = has_attr ? uint32_t(ainfo.size / src.point_count) : 0;
  const uint8_t *av = has_attr ? static_cast<const uint8_t *>(ainfo.data) : nullptr;

  node.vertex_data = std::make_shared<uint8_t[]>(size_t(n) * vstride);
  node.rep_level_data = std::make_shared<uint8_t[]>(size_t(n));
  if (has_attr)
    node.attribute_data = std::make_shared<uint8_t[]>(size_t(n) * astride);

  for (uint32_t j = 0; j < n; j++)
  {
    const uint32_t global = node.first_index + rep_local[perm[j]];
    std::memcpy(node.vertex_data.get() + size_t(j) * vstride, dv + size_t(global) * vstride, vstride);
    if (has_attr)
      std::memcpy(node.attribute_data.get() + size_t(j) * astride, av + size_t(global) * astride, astride);
    node.rep_level_data[j] = rep_level[j];
  }
  node.prefix_count = prefix_count;
  node.draw_count = n;
}

void materialize_virtual_node(virtual_node_t &node, const resident_source_t &src, const std::vector<float> &lod_random_offsets)
{
  switch (src.morton_type)
  {
  case points_type_m32: materialize_typed<morton::morton32_t::component_type, morton::morton32_t::component_count::value>(node, src, lod_random_offsets); break;
  case points_type_m64: materialize_typed<morton::morton64_t::component_type, morton::morton64_t::component_count::value>(node, src, lod_random_offsets); break;
  case points_type_m128: materialize_typed<morton::morton128_t::component_type, morton::morton128_t::component_count::value>(node, src, lod_random_offsets); break;
  case points_type_m192: materialize_typed<morton::morton192_t::component_type, morton::morton192_t::component_count::value>(node, src, lod_random_offsets); break;
  default: break;
  }
}

// ------------------------------------------------------------------ per-frame walk / upload / emit / evict

static void evict_virtual_node(virtual_node_t &v, render::callback_manager_t &callbacks, size_t *gpu_memory_used)
{
  if (v.gpu_state == render_node_gpu_state::uploaded)
  {
    for (auto &b : v.gpu_buffers)
      if (b.user_ptr)
        callbacks.do_destroy_buffer(b);
    if (v.params_buffer.user_ptr)
      callbacks.do_destroy_buffer(v.params_buffer);
    if (gpu_memory_used)
      *gpu_memory_used -= v.gpu_memory_size;
    v.gpu_state = render_node_gpu_state::none;
  }
  v.mat_state = virtual_mat_state::none;
  v.vertex_data.reset();
  v.attribute_data.reset();
  v.rep_level_data.reset();
  v.gpu_memory_size = 0;
}

static void upload_virtual_node(virtual_node_t &v, const resident_source_t &src, const virtual_frame_t &f)
{
  const uint32_t n = v.draw_count;
  const uint32_t vbytes = n * 3u * uint32_t(sizeof(float));
  f.callbacks->do_create_buffer(v.gpu_buffers[0], points_buffer_type_vertex);
  f.callbacks->do_initialize_buffer(v.gpu_buffers[0], points_type_r32, points_components_3, int(vbytes), v.vertex_data.get());

  // Color: same contrast stretch as the real path (R6) -- else intensity/scalar attrs render near-black on
  // promoted regions. RGB (u16x3) is drawn GL-normalized as-is.
  uint32_t abytes = 0;
  f.callbacks->do_create_buffer(v.gpu_buffers[1], points_buffer_type_vertex);
  if (v.attribute_data && src.point_count && src.data_handler->data_info[1].size)
  {
    const auto attr_fmt = src.data_handler->point_format[1];
    const uint32_t astride = uint32_t(src.data_handler->data_info[1].size / src.point_count);
    abytes = n * astride;
    if (attribute_should_normalize(attr_fmt.type, attr_fmt.components, f.attr_min, f.attr_max))
    {
      uint32_t norm_size = 0;
      auto norm = normalize_attribute_to_float(v.attribute_data.get(), abytes, attr_fmt.type, attr_fmt.components, n, f.attr_min, f.attr_max, norm_size);
      f.callbacks->do_initialize_buffer(v.gpu_buffers[1], points_type_r32, attr_fmt.components, int(norm_size), norm.get());
      abytes = norm_size;
    }
    else
    {
      f.callbacks->do_initialize_buffer(v.gpu_buffers[1], attr_fmt.type, attr_fmt.components, int(abytes), v.attribute_data.get());
    }
  }

  const auto offset = glm::dvec3(f.tree_config->offset[0], f.tree_config->offset[1], f.tree_config->offset[2]) + glm::dvec3(src.decode_offset[0], src.decode_offset[1], src.decode_offset[2]);
  v.camera_view = glm::mat4(f.camera->projection * glm::translate(f.camera->view, offset));
  f.callbacks->do_create_buffer(v.gpu_buffers[2], points_buffer_type_uniform);
  f.callbacks->do_initialize_buffer(v.gpu_buffers[2], points_type_r32, points_components_4x4, sizeof(v.camera_view), &v.camera_view);

  // rep_level (u8, one per point) -> per-point density cull in the shader (R19: the Density slider now thins
  // promoted regions the same as everything else).
  f.callbacks->do_create_buffer(v.gpu_buffers[3], points_buffer_type_vertex);
  f.callbacks->do_initialize_buffer(v.gpu_buffers[3], points_type_u8, points_components_1, int(n), v.rep_level_data.get());

  v.draw_type = (src.data_handler->point_format[1].components == points_components_3) ? points_dyn_points_3 : points_dyn_points_1;
  v.gpu_memory_size = vbytes + abytes + n + uint32_t(sizeof(v.camera_view));
  v.gpu_state = render_node_gpu_state::uploaded;
  v.fade_state = render_node_fade_state::fade_in; // crossfade in (R17)
  v.fade_ms = 0.0f;
  if (f.gpu_memory_used)
    *f.gpu_memory_used += v.gpu_memory_size;
  v.vertex_data.reset(); // GPU has it now; the resident source stays for other virtual nodes
  v.attribute_data.reset();
  v.rep_level_data.reset();
}

struct virtual_cut_stats_t
{
  int selected = 0;        // nodes the walk visited this frame (root..frontier)
  int uploaded = 0;        // of those, how many have their GPU buffers ready
  bool animating = false;  // a node is materializing or fading -> the frame loop must keep ticking
};

static void walk_virtual(virtual_node_t &v, const resident_source_t &src, const virtual_frame_t &f, render::frustum_t &frustum)
{
  const glm::dvec3 nearest = glm::clamp(f.camera_position, v.tight_aabb.min, v.tight_aabb.max);
  v.cached_distance = glm::length(nearest - f.camera_position);
  if (frustum.test_aabb(v.loose_aabb.min, v.loose_aabb.max) == render::frustum_intersection_t::outside)
    return; // not selected -> deferred-evicted once its TTL lapses (no clear pass needed)
  v.last_selected_frame = f.frame_index;

  // Hysteresis (R2), mirroring the real walker: a node subdivided last frame uses a lower threshold, so a node
  // parked at the LOD boundary doesn't flip selected/unselected (and re-quantize) every frame under motion.
  // FLOOR the recursion at subdivide_floor_lod = max(prev frame's finest real-node lod, full-detail level): the
  // virtual tree must never invent LOD finer than the real octree renderer is actually showing, and below the
  // full-detail level maskWidth is already 0 (every point drawn). This also stops a dense cluster of coincident
  // points -- which keeps landing in one octant so src_count never drops -- from recursing the level past 0 into
  // NEGATIVE morton levels (out-of-bounds child-mask writes -> hard abort when zoomed close).
  const bool subdivide = v.level > f.subdivide_floor_lod && v.src_count > f.virtual_min_points && should_subdivide(*f.lod_params, v.loose_aabb, v.subdivided_last_frame);
  v.subdivided_last_frame = subdivide;

  if (v.mat_state == virtual_mat_state::none)
  {
    v.mat_state = virtual_mat_state::materializing;
    v.convert_done.store(false, std::memory_order_relaxed);
    virtual_node_t *vp = &v;
    const resident_source_t *sp = &src;
    const std::vector<float> *offs = f.lod_random_offsets;
    f.convert_pool->enqueue([vp, sp, offs] {
      materialize_virtual_node(*vp, *sp, *offs);
      vp->convert_done.store(true, std::memory_order_release);
    });
  }

  if (subdivide)
  {
    if (!v.children_built)
      split_octants(v, src, *f.tree_config);
    for (auto &c : v.children)
      if (c)
        walk_virtual(*c, src, f, frustum);
  }
}

// True iff no descendant holds a GPU buffer or an in-flight/materialized job -> its child structs are safe to
// drop (R18). A materializing node is NOT idle, so pruning never frees a node with a running convert job.
static bool subtree_all_idle(const virtual_node_t &v)
{
  for (auto &c : v.children)
    if (c)
    {
      if (c->mat_state != virtual_mat_state::none || c->gpu_state != render_node_gpu_state::none)
        return false;
      if (!subtree_all_idle(*c))
        return false;
    }
  return true;
}

// One recursion that advances IO, uploads the selected, TTL-evicts the unselected, and tallies cut coverage
// (R12: folds the old clear_selected + any_uploaded_selected passes into this single traversal).
static void process_virtual_node(virtual_node_t &v, const resident_source_t &src, const virtual_frame_t &f, virtual_cut_stats_t &cut)
{
  const bool selected = (v.last_selected_frame == f.frame_index);
  if (selected)
  {
    cut.selected++;
    if (v.mat_state == virtual_mat_state::materializing)
      cut.animating = true; // decode in flight -> keep requesting frames so it uploads once ready
    if (v.mat_state == virtual_mat_state::materializing && v.convert_done.load(std::memory_order_acquire))
      v.mat_state = virtual_mat_state::materialized;
    if (v.mat_state == virtual_mat_state::materialized && v.gpu_state == render_node_gpu_state::none)
    {
      // Unified GPU budget (R7): admit a virtual upload only if real (this frame) + virtual (running) + this
      // node fit. The walk is additive root->frontier, so coarse ancestors are admitted before fine octants.
      const auto &afmt = src.data_handler->point_format[1];
      uint32_t astride = (src.data_handler->data_info[1].data && src.point_count) ? uint32_t(src.data_handler->data_info[1].size / src.point_count) : 0;
      if (astride && attribute_should_normalize(afmt.type, afmt.components, f.attr_min, f.attr_max))
        astride = uint32_t(afmt.components) * uint32_t(sizeof(float)); // normalized attrs upload widened to r32 (bug #3)
      const size_t est_bytes = size_t(v.draw_count) * (3u * sizeof(float) + astride + 1u) + sizeof(v.camera_view);
      // Don't count this leaf's own still-resident monolith against its own cut (bug #1): real_gpu_used includes
      // it while unfreed, so the cut could never grow into the space its monolith will vacate -> deadlock.
      const size_t real_less_own = f.real_gpu_used > f.own_monolith_bytes ? f.real_gpu_used - f.own_monolith_bytes : 0;
      if (f.gpu_memory_budget == 0 || real_less_own + *f.gpu_memory_used + est_bytes <= f.gpu_memory_budget)
        upload_virtual_node(v, src, f);
    }
    if (v.gpu_state == render_node_gpu_state::uploaded)
    {
      cut.uploaded++;
      if (v.fade_state == render_node_fade_state::fade_in) // crossfade in (R17)
      {
        cut.animating = true;
        v.fade_ms += f.delta_ms;
        if (v.fade_ms >= f.fade_duration_ms)
        {
          v.fade_ms = f.fade_duration_ms;
          v.fade_state = render_node_fade_state::steady;
        }
      }
    }
  }
  else if (v.mat_state != virtual_mat_state::none)
  {
    // Deferred eviction (R9): hold an unselected node for a TTL so a brief deselect (panning across a cut, a
    // frustum clip) resumes with zero rework. Never free while a materialize job is still in flight.
    const bool in_flight = (v.mat_state == virtual_mat_state::materializing && !v.convert_done.load(std::memory_order_acquire));
    const uint32_t age = f.frame_index - v.last_selected_frame;
    if (!in_flight && age > f.evict_ttl_frames)
      evict_virtual_node(v, *f.callbacks, f.gpu_memory_used);
  }
  for (auto &c : v.children)
    if (c)
      process_virtual_node(*c, src, f, cut);

  // R18: reclaim the struct subtree of a long-cold node (re-descent cheaply rebuilds it from the immutable
  // resident codes). Guarded on subtree_all_idle so no in-flight/uploaded descendant is dropped.
  if (v.children_built && v.last_selected_frame != f.frame_index && (f.frame_index - v.last_selected_frame) > f.prune_ttl_frames && subtree_all_idle(v))
  {
    for (auto &c : v.children)
      c.reset();
    v.children_built = false;
  }
}

// R3: free the promoted leaf's own monolith GPU buffers once the virtual cut has been stably covering it. The
// additive virtual cut always keeps coverage (ancestors stay resident), so the monolith is pure waste while
// suppressed. io_state is reset so an un-promotion (CPU eviction / A-B off) reloads it cleanly from disk; the
// IO scan skips monolith_freed nodes so they are NOT re-loaded while the cut is live.
static void free_monolith(render_node_t &node, render::callback_manager_t &callbacks)
{
  for (auto &b : node.gpu_buffers)
    if (b.user_ptr)
      callbacks.do_destroy_buffer(b);
  if (node.params_buffer.user_ptr)
    callbacks.do_destroy_buffer(node.params_buffer);
  node.gpu_state = render_node_gpu_state::none;
  node.io_state = render_node_io_state::none;
  node.monolith_freed = true;
  node.gpu_memory_size = 0;
}

void process_virtual_trees(render_list_t &render_list, virtual_frame_t &f)
{
  render::frustum_t frustum;
  frustum.update(f.camera->view_projection);
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (!node.is_virtual_source || !node.virtual_root || !node.resident)
      continue;
    if (node.fade_state == render_node_fade_state::fade_out)
    {
      // Departing (R10): let the monolith's own crossfade play; don't also draw the virtual cut (double draw).
      // KNOWN COSMETIC LIMITATION (review bug #4): if R3 already freed the monolith, there's nothing to fade,
      // so this node's detail vanishes in one frame. The additive coarser ancestors keep drawing, so it's a
      // density pop on zoom-out, never a blank hole (and it's off-screen when the departure is a frustum exit).
      // A proper fix is a virtual-cut fade-out, deferred as a larger crossfade feature.
      node.draw_suppressed = false;
      continue;
    }
    f.own_monolith_bytes = node.monolith_freed ? 0 : node.gpu_memory_size; // bug #1: exclude from this cut's gate
    walk_virtual(*node.virtual_root, *node.resident, f, frustum);
    virtual_cut_stats_t cut;
    process_virtual_node(*node.virtual_root, *node.resident, f, cut);
    // R16: replace the monolith only when the WHOLE selected cut is uploaded (frontier-complete), so the
    // handoff never briefly shows a coarser/partially-loaded cut than the monolith already had.
    const bool cut_complete = cut.selected > 0 && cut.uploaded == cut.selected;
    // Keep ticking only on genuine progress (materialize/fade in flight), NOT on a mere incomplete cut: a cut
    // stalled by a saturated budget is static state (re-armed by camera motion), so pegging is_animating on
    // !cut_complete would busy-loop the on-demand renderer forever (bug #1).
    f.any_animating = f.any_animating || cut.animating;
    node.virtual_cut_live_frames = cut_complete ? node.virtual_cut_live_frames + 1 : 0;
    node.draw_suppressed = cut_complete || node.monolith_freed;
    if (node.virtual_cut_live_frames >= f.monolith_free_after_frames && node.gpu_state == render_node_gpu_state::uploaded && !node.monolith_freed)
      free_monolith(node, *f.callbacks); // R3
  }
}

struct virtual_emit_ctx_t
{
  render::callback_manager_t *callbacks;
  const render::frame_camera_cpp_t *camera;
  const tree_config_t *tree_config;
  points_to_render_t *to_render;
  uint32_t frame_index;
  int viewport_height;
  double render_density_px;
  float fade_duration_ms;
  float lod_px_scale;      // per-frame per-point-LOD constants (identical to the real emit)
  float lod_density_scale;
};

static void emit_virtual_node(virtual_node_t &v, const resident_source_t &src, const virtual_emit_ctx_t &e, uint64_t &points_rendered, int &drawn)
{
  if (v.last_selected_frame == e.frame_index && v.gpu_state == render_node_gpu_state::uploaded && v.draw_count > 0)
  {
    const auto offset = glm::dvec3(e.tree_config->offset[0], e.tree_config->offset[1], e.tree_config->offset[2]) + glm::dvec3(src.decode_offset[0], src.decode_offset[1], src.decode_offset[2]);
    v.camera_view = glm::mat4(e.camera->projection * glm::translate(e.camera->view, offset));
    e.callbacks->do_modify_buffer(v.gpu_buffers[2], 0, sizeof(v.camera_view), &v.camera_view);

    // Same per-point LOD submit bound + shader density cull as the real path (R19: the Density slider now
    // thins promoted regions too, instead of the old lod_density_scale=0 keep-all).
    const uint32_t draw_size = lod_draw_size_from_prefix(v.prefix_count, v.draw_count, v.cached_distance, e.camera->projection[1][1], e.viewport_height, e.tree_config->scale, e.render_density_px);

    v.draw_list[0] = {points_dyn_points_bm_vertex, v.gpu_buffers[0].user_ptr};
    v.draw_list[1] = {points_dyn_points_bm_color, v.gpu_buffers[1].user_ptr};
    v.draw_list[2] = {points_dyn_points_bm_camera, v.gpu_buffers[2].user_ptr};
    if (v.fade_state == render_node_fade_state::fade_in)
    {
      // Whole-node crossfade in (R17) via the same crossfade draw + params alpha the real nodes use.
      float alpha = v.fade_ms / e.fade_duration_ms;
      alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
      const bool is_mono = (v.draw_type == points_dyn_points_1);
      v.params_data = glm::vec4(alpha, 1.0f, is_mono ? 1.0f : 0.0f, is_mono ? 1.0f : 0.0f);
      if (!v.params_buffer.user_ptr)
      {
        e.callbacks->do_create_buffer(v.params_buffer, points_buffer_type_uniform);
        e.callbacks->do_initialize_buffer(v.params_buffer, points_type_r32, points_components_4, sizeof(v.params_data), &v.params_data);
      }
      else
        e.callbacks->do_modify_buffer(v.params_buffer, 0, sizeof(v.params_data), &v.params_data);
      v.draw_list[3] = {points_dyn_points_bm_old_color, v.gpu_buffers[1].user_ptr};
      v.draw_list[4] = {points_dyn_points_bm_params, v.params_buffer.user_ptr};
      v.draw_list[5] = {points_dyn_points_bm_replevel, v.gpu_buffers[3].user_ptr};
      points_draw_group_t draw_group = {points_dyn_points_crossfade, v.draw_list, 6, int(draw_size), v.level, e.lod_px_scale, e.lod_density_scale};
      points_to_render_add_render_group(e.to_render, draw_group);
    }
    else
    {
      v.draw_list[3] = {points_dyn_points_bm_replevel, v.gpu_buffers[3].user_ptr};
      points_draw_group_t draw_group = {v.draw_type, v.draw_list, 4, int(draw_size), v.level, e.lod_px_scale, e.lod_density_scale};
      points_to_render_add_render_group(e.to_render, draw_group);
    }
    points_rendered += draw_size;
    drawn++;
  }
  for (auto &c : v.children)
    if (c)
      emit_virtual_node(*c, src, e, points_rendered, drawn);
}

int emit_virtual_draws(render_list_t &render_list, render::callback_manager_t &callbacks, const render::frame_camera_cpp_t &camera, const tree_config_t &tree_config, points_to_render_t *to_render, uint32_t frame_index, int viewport_height, double render_density_px, float fade_duration_ms, uint64_t &points_rendered)
{
  int drawn = 0;
  virtual_emit_ctx_t e;
  e.callbacks = &callbacks;
  e.camera = &camera;
  e.tree_config = &tree_config;
  e.to_render = to_render;
  e.frame_index = frame_index;
  e.viewport_height = viewport_height;
  e.render_density_px = render_density_px;
  e.fade_duration_ms = fade_duration_ms;
  e.lod_px_scale = float(camera.projection[1][1] * 0.5 * double(viewport_height));
  e.lod_density_scale = tree_config.scale > 0.0 ? float(render_density_px / tree_config.scale) : 0.0f;
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (!node.is_virtual_source || !node.virtual_root || !node.resident)
      continue;
    emit_virtual_node(*node.virtual_root, *node.resident, e, points_rendered, drawn);
  }
  return drawn;
}

static void destroy_virtual_node_recursive(virtual_node_t &v, render::callback_manager_t &callbacks, size_t *gpu_memory_used)
{
  for (auto &c : v.children)
    if (c)
      destroy_virtual_node_recursive(*c, callbacks, gpu_memory_used);
  if (v.mat_state == virtual_mat_state::materializing)
    while (!v.convert_done.load(std::memory_order_acquire))
      std::this_thread::yield();
  evict_virtual_node(v, callbacks, gpu_memory_used);
}

void destroy_virtual_subtree(std::unique_ptr<virtual_node_t> &root, render::callback_manager_t &callbacks, size_t *gpu_memory_used)
{
  if (!root)
    return;
  destroy_virtual_node_recursive(*root, callbacks, gpu_memory_used);
  root.reset();
}

} // namespace points::converter
