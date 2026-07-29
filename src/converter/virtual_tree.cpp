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
  if (node.is_leaf)
    materialize_leaf(node, src);
  else
    materialize_interior(node, src, lod_random_offsets);
}

} // namespace points::converter
