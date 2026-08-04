/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

// The LOD representative-selection scheme, shared by BOTH the converter's bottom-up LOD generation
// (tree_lod_generator) AND the renderer's runtime virtual-subnode LOD. Keeping it in one place is what
// guarantees a virtual LOD node renders pixel-identically to a real (stored) LOD node at the same level:
// one representative per morton grid cell of width maskWidth = max(0, lod - 9), with the same deterministic
// per-cell pick driven by the same random offsets (mt19937(4244)).

#include "morton.hpp"
#include "dataset_types.hpp"
#include <dew/converter/converter.h>

#include <random>
#include <vector>

namespace dew::converter
{
using namespace dew::core;

// maskWidth = max(0, lod - lod_quantize_full_detail_level). At/below this octree level a node's morton cell is
// a single point (every point is its own representative -> the finest LOD draws all points). 3 morton bits per
// axis * 3 axes = 9. Shared by the converter LOD generator, the virtual-node materialization, AND the
// promotion gate (promote a leaf only if lod_span > this, i.e. it has a genuinely coarser representation).
inline constexpr int lod_quantize_full_detail_level = 3 * 3;

// maskWidth for an octree/virtual node at `lod`: how many low morton bits to collapse when picking one
// representative per cell. Keeps the converter and the renderer's virtual LOD byte-identical.
inline constexpr int lod_quantize_mask_width(int lod)
{
  return lod > lod_quantize_full_detail_level ? lod - lod_quantize_full_detail_level : 0;
}

template <typename T, size_t N>
struct morton_to_lod_t
{
  morton::morton_t<T, N> morton;
  offset_in_subset_t index;
  input_data_id_t id;
};

// The fixed random offsets used to pick a representative within each morton cell. Seed 4244 is load-bearing:
// the converter's stored LOD nodes were generated with these exact offsets, so a virtual LOD node must reuse
// them to select the same points. 256 deterministic floats in [0,1).
inline std::vector<float> make_lod_random_offsets()
{
  std::vector<float> offsets(256);
  std::mt19937 gen(4244);
  std::uniform_real_distribution<float> dis(0.0f, 1.0f);
  for (auto &o : offsets)
    o = dis(gen);
  return offsets;
}

// Scan a morton-sorted [offset, offset+point_count) window of `source` and emit ONE representative per
// occupied cell of width maskWidth: a cell closes when a point exceeds create_max(maskWidth, cell_start); the
// representative is picked at range_start + random_offsets[...]*range_size (the final cell uses the median).
// The emitted morton is cast into node_min's local frame; index is the global index into the source.
template <typename S_M, typename T, size_t N>
inline void find_indices_to_quantize(input_data_id_t input_id, const morton::morton192_t &node_min, const dew_converter_buffer_t &source, offset_in_subset_t offset, point_count_t point_count, int maskWidth,
                                     const std::vector<float> &random_offsets, std::vector<morton_to_lod_t<T, N>> &morton_to_lod)
{
  auto *source_it = reinterpret_cast<const S_M *>(source.data);
  assert(source.size % sizeof(S_M) == 0);
  assert(source_it + point_count.data == source_it + (source.size / sizeof(S_M)));
  // Clamp the cell mask to what the SOURCE morton type can express. A unit's stored values are
  // truncations of the absolute code, and its lod span fits the type -- so every point shares the
  // bits above the type width, and a quantize cell wider than the type groups exactly like the
  // widest expressible cell. Collapsed leaves are narrow (often m32) while the parent's mask width
  // comes from the parent lod, so the unclamped mask can exceed the type (mask_create asserts).
  constexpr int max_mask_for_type = (int(sizeof(S_M) * 8) - 4) / 3;
  maskWidth = maskWidth < max_mask_for_type ? maskWidth : max_mask_for_type;
  uint32_t range_start = 0;
  S_M currentMaxVal = morton::create_max(maskWidth, *source_it);
  for (uint32_t i = 1; i < point_count.data; i++)
  {
    if (source_it[i] <= currentMaxVal)
      continue;

    auto range_size = i - range_start;
    auto index_into_random_offsets = (range_start + (range_size / 2)) % random_offsets.size();
    auto index = range_start + uint32_t(random_offsets[index_into_random_offsets] * (range_size));
    auto &to_lod = morton_to_lod.emplace_back();
    morton::morton_cast(source_it[index], node_min, to_lod.morton);
    to_lod.index.data = offset.data + index;
    to_lod.id = input_id;

    range_start = i;
    currentMaxVal = morton::create_max(maskWidth, source_it[i]);
  }
  auto index = range_start + ((point_count.data - range_start) / 2);
  auto &to_lod = morton_to_lod.emplace_back();
  to_lod.id = input_id;
  morton::morton_cast(source_it[index], node_min, to_lod.morton);
  to_lod.index.data = offset.data + index;
}

template <typename T, size_t N>
inline void find_indices_to_quantize(input_data_id_t input_id, const morton::morton192_t &node_min, dew_type_t source_type, const dew_converter_buffer_t &source, offset_in_subset_t offset, point_count_t point_count, int maskWidth,
                                     const std::vector<float> &random_offsets, std::vector<morton_to_lod_t<T, N>> &morton_to_lod)
{
  assert(source_type == dew_type_m32 || source_type == dew_type_m64 || source_type == dew_type_m128 || source_type == dew_type_m192);

  switch (source_type)
  {
  case dew_type_m32:
    return find_indices_to_quantize<morton::morton32_t>(input_id, node_min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case dew_type_m64:
    return find_indices_to_quantize<morton::morton64_t>(input_id, node_min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case dew_type_m128:
    return find_indices_to_quantize<morton::morton128_t>(input_id, node_min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case dew_type_m192:
    return find_indices_to_quantize<morton::morton192_t>(input_id, node_min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  default:
    return;
  }
}

} // namespace dew::converter
