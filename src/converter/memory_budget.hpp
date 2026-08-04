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

// Pre-load per-node byte estimators for the render IO scheduler. Given only walker data -- point count,
// attribute formats and blob locations, all known BEFORE any IO -- they predict the decode inputs, the CPU
// bytes the decode outputs will hold, and the GPU bytes the upload will charge, so the scheduler can refuse
// work whose peak would not fit inside the budget.
//
// The budget derivation and the heap-pressure brake are storage/walker-free and live in the core's
// budget.hpp, included below.

#include "budget.hpp" // derived_budgets_t, derive_budgets, brake_level_t, compute_brake_level

#include "frustum_tree_walker.hpp"
#include "input_header.hpp"

#include <algorithm>
#include <cstdint>

namespace dew::converter
{

// Decode-input bytes for one node (morton codes + attribute blob, decompressed). These stay alive via
// _impl_data's data_handler from load start until the post-upload reap. Known before any IO from walker data.
inline uint64_t estimate_node_input_bytes(const tree_walker_data_t &w)
{
  const uint64_t pc = w.point_count.data;
  const uint64_t input_stride = uint64_t(size_for_format(w.format[0].type, w.format[0].components));
  const uint64_t attr_stride = w.locations[1].size > 0 ? uint64_t(size_for_format(w.format[1].type, w.format[1].components)) : 0;
  return pc * (input_stride + attr_stride);
}

// Pre-load CPU-byte estimate for one node, from walker data alone (point_count/format/locations are known
// before any IO). Counts the decode inputs (see above) plus the decode outputs (r32x3 vertex + attribute +
// rep_level). The worker-loader path holds compressed (smaller) inputs in the render heap, so this
// overcounts there -- safe: it only throttles IO slightly earlier.
inline uint64_t estimate_node_cpu_bytes(const tree_walker_data_t &w)
{
  const uint64_t pc = w.point_count.data;
  const uint64_t attr_stride = w.locations[1].size > 0 ? uint64_t(size_for_format(w.format[1].type, w.format[1].components)) : 0;
  return estimate_node_input_bytes(w) + pc * (3 * sizeof(float) + attr_stride + 1);
}

// Pre-load GPU-byte estimate matching what the upload path charges (node.gpu_memory_size): r32x3 vertex +
// attribute + u8 rep_level + the camera mat4 and params vec4 uniforms. Attributes normalize to
// float-per-component at upload EXCEPT u16x3 (rgb -- the common case, uploaded raw; mirrors
// should_normalize in process_io_and_upload), so take the larger of raw and normalized for the rest.
inline uint64_t estimate_node_gpu_bytes(const tree_walker_data_t &w)
{
  const uint64_t pc = w.point_count.data;
  uint64_t attr_stride = 0;
  if (w.locations[1].size > 0)
  {
    const uint64_t raw = uint64_t(size_for_format(w.format[1].type, w.format[1].components));
    const uint64_t normalized = uint64_t(w.format[1].components) * sizeof(float);
    const bool never_normalized = w.format[1].type == dew_type_u16 && w.format[1].components == dew_components_3;
    attr_stride = never_normalized ? raw : std::max(raw, normalized);
  }
  return pc * (3 * sizeof(float) + attr_stride + 1) + 16 * sizeof(float) + 4 * sizeof(float);
}

} // namespace dew::converter
