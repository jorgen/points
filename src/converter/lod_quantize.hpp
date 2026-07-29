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

// The LOD representative-selection scheme, shared by BOTH the converter's bottom-up LOD generation
// (tree_lod_generator) AND the renderer's runtime virtual-subnode LOD. Keeping it in one place is what
// guarantees a virtual LOD node renders pixel-identically to a real (stored) LOD node at the same level:
// one representative per morton grid cell of width maskWidth = max(0, lod - 9), with the same deterministic
// per-cell pick driven by the same random offsets (mt19937(4244)).

#include "morton.hpp"
#include "conversion_types.hpp"
#include <points/converter/converter.h>

#include <random>
#include <vector>

namespace points::converter
{

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
inline void find_indices_to_quantize(input_data_id_t input_id, const morton::morton192_t &node_min, const points_converter_buffer_t &source, offset_in_subset_t offset, point_count_t point_count, int maskWidth,
                                     const std::vector<float> &random_offsets, std::vector<morton_to_lod_t<T, N>> &morton_to_lod)
{
  auto *source_it = reinterpret_cast<const S_M *>(source.data);
  assert(source.size % sizeof(S_M) == 0);
  assert(source_it + point_count.data == source_it + (source.size / sizeof(S_M)));
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
inline void find_indices_to_quantize(input_data_id_t input_id, const morton::morton192_t &node_min, points_type_t source_type, const points_converter_buffer_t &source, offset_in_subset_t offset, point_count_t point_count, int maskWidth,
                                     const std::vector<float> &random_offsets, std::vector<morton_to_lod_t<T, N>> &morton_to_lod)
{
  assert(source_type == points_type_m32 || source_type == points_type_m64 || source_type == points_type_m128 || source_type == points_type_m192);

  switch (source_type)
  {
  case points_type_m32:
    return find_indices_to_quantize<morton::morton32_t>(input_id, node_min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case points_type_m64:
    return find_indices_to_quantize<morton::morton64_t>(input_id, node_min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case points_type_m128:
    return find_indices_to_quantize<morton::morton128_t>(input_id, node_min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  case points_type_m192:
    return find_indices_to_quantize<morton::morton192_t>(input_id, node_min, source, offset, point_count, maskWidth, random_offsets, morton_to_lod);
  default:
    return;
  }
}

} // namespace points::converter
