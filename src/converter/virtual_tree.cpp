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

static void materialize_leaf(virtual_node_t &node, const resident_source_t &src)
{
  const uint32_t n = node.src_count;
  const uint32_t vstride = 3u * uint32_t(sizeof(float));
  node.vertex_data = std::make_shared<uint8_t[]>(size_t(n) * vstride);
  std::memcpy(node.vertex_data.get(), src.decoded_vertex.get() + size_t(node.first_index) * vstride, size_t(n) * vstride);

  const auto &ainfo = src.data_handler->data_info[1];
  if (ainfo.data && ainfo.size && src.point_count)
  {
    const uint32_t astride = ainfo.size / src.point_count;
    node.attribute_data = std::make_shared<uint8_t[]>(size_t(n) * astride);
    std::memcpy(node.attribute_data.get(), static_cast<const uint8_t *>(ainfo.data) + size_t(node.first_index) * astride, size_t(n) * astride);
  }
  node.draw_count = n;
}

static void materialize_interior(virtual_node_t &node, const resident_source_t &src, const std::vector<float> &offs)
{
  const int maskWidth = std::max(0, node.level - 3 * 3); // == the converter's max(0, lod-9)
  const uint32_t code_size = morton_type_size(src.morton_type);
  points_converter_buffer_t source_buf(static_cast<uint8_t *>(src.data_handler->data_info[0].data) + size_t(node.first_index) * code_size, node.src_count * code_size);

  using M192 = morton::morton192_t;
  std::vector<morton_to_lod_t<M192::component_type, M192::component_count::value>> reps;
  input_data_id_t dummy_id{0, 0};
  find_indices_to_quantize(dummy_id, node.octant_min, src.morton_type, source_buf, offset_in_subset_t(node.first_index), point_count_t(node.src_count), maskWidth, offs, reps);

  const uint32_t m = uint32_t(reps.size());
  const uint32_t vstride = 3u * uint32_t(sizeof(float));
  node.vertex_data = std::make_shared<uint8_t[]>(size_t(m) * vstride);
  const uint8_t *dv = src.decoded_vertex.get();
  for (uint32_t j = 0; j < m; j++)
    std::memcpy(node.vertex_data.get() + size_t(j) * vstride, dv + size_t(reps[j].index.data) * vstride, vstride);

  const auto &ainfo = src.data_handler->data_info[1];
  if (ainfo.data && ainfo.size && src.point_count)
  {
    const uint32_t astride = ainfo.size / src.point_count;
    node.attribute_data = std::make_shared<uint8_t[]>(size_t(m) * astride);
    const uint8_t *av = static_cast<const uint8_t *>(ainfo.data);
    for (uint32_t j = 0; j < m; j++)
      std::memcpy(node.attribute_data.get() + size_t(j) * astride, av + size_t(reps[j].index.data) * astride, astride);
  }
  node.draw_count = m;
}

void materialize_virtual_node(virtual_node_t &node, const resident_source_t &src, const std::vector<float> &lod_random_offsets)
{
  // Every virtual node draws at its level's LOD, exactly like a stored LOD node: one representative per
  // maskWidth=max(0,lod-9) morton cell. LOD is additive (the walker draws root->frontier), so a far leaf's
  // root draws a coarse subsample and finer octants add in as the camera approaches. maskWidth==0 (level<=9,
  // the finest) means every point is its own cell -> draw them all (fast path).
  const int maskWidth = std::max(0, node.level - 3 * 3);
  if (maskWidth == 0)
    materialize_leaf(node, src);
  else
    materialize_interior(node, src, lod_random_offsets);
}

// ------------------------------------------------------------------ per-frame walk / upload / emit / evict

static void clear_selected(virtual_node_t &v)
{
  v.selected_this_frame = false;
  for (auto &c : v.children)
    if (c)
      clear_selected(*c);
}

static void evict_virtual_node(virtual_node_t &v, render::callback_manager_t &callbacks, size_t *gpu_memory_used)
{
  if (v.gpu_state == render_node_gpu_state::uploaded)
  {
    for (auto &b : v.gpu_buffers)
      if (b.user_ptr)
        callbacks.do_destroy_buffer(b);
    if (gpu_memory_used)
      *gpu_memory_used -= v.gpu_memory_size;
    v.gpu_state = render_node_gpu_state::none;
  }
  v.mat_state = virtual_mat_state::none;
  v.vertex_data.reset();
  v.attribute_data.reset();
  v.gpu_memory_size = 0;
}

static void upload_virtual_node(virtual_node_t &v, const resident_source_t &src, const virtual_frame_t &f)
{
  const uint32_t vbytes = v.draw_count * 3u * uint32_t(sizeof(float));
  f.callbacks->do_create_buffer(v.gpu_buffers[0], points_buffer_type_vertex);
  f.callbacks->do_initialize_buffer(v.gpu_buffers[0], points_type_r32, points_components_3, int(vbytes), v.vertex_data.get());

  uint32_t abytes = 0;
  f.callbacks->do_create_buffer(v.gpu_buffers[1], points_buffer_type_vertex);
  if (v.attribute_data && src.point_count && src.data_handler->data_info[1].size)
  {
    const auto attr_fmt = src.data_handler->point_format[1];
    const uint32_t astride = src.data_handler->data_info[1].size / src.point_count;
    abytes = v.draw_count * astride;
    f.callbacks->do_initialize_buffer(v.gpu_buffers[1], attr_fmt.type, attr_fmt.components, int(abytes), v.attribute_data.get());
  }

  const auto offset = glm::dvec3(f.tree_config->offset[0], f.tree_config->offset[1], f.tree_config->offset[2]) + glm::dvec3(src.decode_offset[0], src.decode_offset[1], src.decode_offset[2]);
  v.camera_view = glm::mat4(f.camera->projection * glm::translate(f.camera->view, offset));
  f.callbacks->do_create_buffer(v.gpu_buffers[2], points_buffer_type_uniform);
  f.callbacks->do_initialize_buffer(v.gpu_buffers[2], points_type_r32, points_components_4x4, sizeof(v.camera_view), &v.camera_view);

  v.gpu_memory_size = vbytes + abytes + uint32_t(sizeof(v.camera_view));
  v.gpu_state = render_node_gpu_state::uploaded;
  if (f.gpu_memory_used)
    *f.gpu_memory_used += v.gpu_memory_size;
  v.vertex_data.reset(); // GPU has it now; the resident source stays for other virtual nodes
  v.attribute_data.reset();
}

static void walk_virtual(virtual_node_t &v, const resident_source_t &src, const virtual_frame_t &f, render::frustum_t &frustum)
{
  const glm::dvec3 nearest = glm::clamp(f.camera_position, v.tight_aabb.min, v.tight_aabb.max);
  v.cached_distance = glm::length(nearest - f.camera_position);
  if (frustum.test_aabb(v.loose_aabb.min, v.loose_aabb.max) == render::frustum_intersection_t::outside)
    return; // not selected -> will be evicted in the process pass
  v.selected_this_frame = true;

  const bool subdivide = v.src_count > f.virtual_min_points && should_subdivide(*f.lod_params, v.loose_aabb, false);

  // Materialize once (level-driven, so the result never changes for this node -> no re-materialize on flip).
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

static void process_virtual_node(virtual_node_t &v, const resident_source_t &src, const virtual_frame_t &f)
{
  if (v.selected_this_frame)
  {
    if (v.mat_state == virtual_mat_state::materializing && v.convert_done.load(std::memory_order_acquire))
      v.mat_state = virtual_mat_state::materialized;
    if (v.mat_state == virtual_mat_state::materialized && v.gpu_state == render_node_gpu_state::none)
      upload_virtual_node(v, src, f);
  }
  else if (v.mat_state != virtual_mat_state::none)
  {
    // Not selected -> evict, but never free while a materialize job is still in flight.
    if (!(v.mat_state == virtual_mat_state::materializing && !v.convert_done.load(std::memory_order_acquire)))
      evict_virtual_node(v, *f.callbacks, f.gpu_memory_used);
  }
  for (auto &c : v.children)
    if (c)
      process_virtual_node(*c, src, f);
}

static bool any_uploaded_selected(const virtual_node_t &v)
{
  if (v.selected_this_frame && v.gpu_state == render_node_gpu_state::uploaded && v.draw_count > 0)
    return true;
  for (auto &c : v.children)
    if (c && any_uploaded_selected(*c))
      return true;
  return false;
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
    clear_selected(*node.virtual_root);
    walk_virtual(*node.virtual_root, *node.resident, f, frustum);
    process_virtual_node(*node.virtual_root, *node.resident, f);
    node.draw_suppressed = any_uploaded_selected(*node.virtual_root);
  }
}

static void emit_virtual_node(virtual_node_t &v, const resident_source_t &src, render::callback_manager_t &callbacks, const render::frame_camera_cpp_t &camera, const tree_config_t &tree_config, points_to_render_t *to_render, uint64_t &points_rendered, int &drawn)
{
  if (v.selected_this_frame && v.gpu_state == render_node_gpu_state::uploaded && v.draw_count > 0)
  {
    const auto offset = glm::dvec3(tree_config.offset[0], tree_config.offset[1], tree_config.offset[2]) + glm::dvec3(src.decode_offset[0], src.decode_offset[1], src.decode_offset[2]);
    v.camera_view = glm::mat4(camera.projection * glm::translate(camera.view, offset));
    callbacks.do_modify_buffer(v.gpu_buffers[2], 0, sizeof(v.camera_view), &v.camera_view);

    v.draw_list[0] = {points_dyn_points_bm_vertex, v.gpu_buffers[0].user_ptr};
    v.draw_list[1] = {points_dyn_points_bm_color, v.gpu_buffers[1].user_ptr};
    v.draw_list[2] = {points_dyn_points_bm_camera, v.gpu_buffers[2].user_ptr};
    // lod_density_scale = 0 -> shader W is very negative -> keeps all points (flat draw; no rep_level needed).
    points_draw_group_t draw_group = {v.draw_type, v.draw_list, 3, int(v.draw_count), v.level, 1.0f, 0.0f};
    points_to_render_add_render_group(to_render, draw_group);
    points_rendered += v.draw_count;
    drawn++;
  }
  for (auto &c : v.children)
    if (c)
      emit_virtual_node(*c, src, callbacks, camera, tree_config, to_render, points_rendered, drawn);
}

int emit_virtual_draws(render_list_t &render_list, render::callback_manager_t &callbacks, const render::frame_camera_cpp_t &camera, const tree_config_t &tree_config, points_to_render_t *to_render, uint64_t &points_rendered)
{
  int drawn = 0;
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (!node.is_virtual_source || !node.virtual_root || !node.resident)
      continue;
    emit_virtual_node(*node.virtual_root, *node.resident, callbacks, camera, tree_config, to_render, points_rendered, drawn);
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
