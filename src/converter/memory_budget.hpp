/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
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

// The renderer's total-CPU-memory budget: one consumer knob (dew_converter_data_source_set_memory_budget)
// derived into the sub-budgets that actually bound heap growth. Kept free of storage/GL dependencies so the
// derivation, the heap-pressure brake, and the pre-load byte estimators are unit-testable natively.
//
// Why this exists: on wasm the emscripten heap NEVER shrinks, so every transient allocation spike ratchets
// the heap size permanently; mobile browsers kill the tab well before the default 2GB ceiling. The budget
// bounds the steady state, the estimators let the IO scheduler refuse work whose peak wouldn't fit, and the
// brake is the backstop that reacts to the real heap when the estimates miss.

#include "frustum_tree_walker.hpp"
#include "input_header.hpp"

#include <algorithm>
#include <cstdint>

namespace dew::converter
{

struct derived_budgets_t
{
  uint64_t total = 0;
  uint64_t read_cache_bytes = 0;         // storage_handler _read_cache (compressed blobs)
  uint64_t decompressed_cache_bytes = 0; // storage_handler _decompressed_cache (inline-decompress readers)
  uint64_t decoded_backlog_cap = 0;      // in-flight + decoded-awaiting-upload CPU bytes across the render list
  uint64_t cpu_resident_budget = 0;      // virtual-subtree resident sources + salvage handlers
  int io_clamp = 0;                      // effective max_in_flight_io = min(user knob, io_clamp)
};

// Split the one total budget B into the pools that bound CPU-heap growth. The remaining ~B/4 is deliberate
// headroom for transients the per-frame accounting cannot see: the reorder copy in decode_node, the worker
// reply copies into the render heap, vio's whole-response fetch buffering, normalize copies, stacks/statics.
// B = 1GB (the default) reproduces the pre-budget defaults exactly (256MB read cache, 256MB resident, io 64),
// so desktop behavior at defaults is unchanged by construction.
inline derived_budgets_t derive_budgets(uint64_t total_bytes)
{
  constexpr uint64_t mb = 1024 * 1024;
  derived_budgets_t d;
  d.total = total_bytes;
  d.read_cache_bytes = std::clamp<uint64_t>(total_bytes / 4, 16 * mb, 256 * mb);
  d.decompressed_cache_bytes = std::clamp<uint64_t>(total_bytes / 16, 8 * mb, 64 * mb);
  d.decoded_backlog_cap = std::clamp<uint64_t>(total_bytes / 4, 24 * mb, 256 * mb);
  d.cpu_resident_budget = std::clamp<uint64_t>(total_bytes / 4, 32 * mb, 256 * mb);
  d.io_clamp = int(std::clamp<uint64_t>(d.decoded_backlog_cap / (4 * mb), 4, 64));
  return d;
}

// Heap-pressure brake. Levels tighten the same per-frame caps the budget derives; `none` below 80% of the
// heap ceiling, `high` at 80%, `critical` at 90%. heap_max == 0 means "no probe" (native) -> none. On wasm
// the level is effectively monotone (the heap never shrinks) and that is intended: once high, the tightened
// caps shrink the steady-state working set to fit inside the already-grown heap, malloc reuses freed space
// within it, and growth events stop -- so the link-time MAXIMUM_MEMORY trap is never reached.
enum class brake_level_t : uint32_t
{
  none = 0,
  high = 1,
  critical = 2,
};

inline brake_level_t compute_brake_level(uint64_t heap_bytes, uint64_t heap_max)
{
  if (heap_max == 0)
    return brake_level_t::none;
  if (heap_bytes * 10 >= heap_max * 9)
    return brake_level_t::critical;
  if (heap_bytes * 10 >= heap_max * 8)
    return brake_level_t::high;
  return brake_level_t::none;
}

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
