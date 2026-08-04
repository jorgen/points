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

// The total-CPU-memory budget: one consumer knob derived into the sub-budgets that actually bound heap
// growth, plus the heap-pressure brake. Free of storage, walker and GL dependencies so the derivation is
// unit-testable natively and usable by any consumer of the core.
//
// Why this exists: on wasm the emscripten heap NEVER shrinks, so every transient allocation spike ratchets
// the heap size permanently; mobile browsers kill the tab well before the default 2GB ceiling. The budget
// bounds the steady state and the brake is the backstop that reacts to the real heap when estimates miss.
//
// The PER-NODE byte estimators that feed the render IO scheduler live in the converter's memory_budget.hpp:
// they take a tree_walker_data_t and count render-shaped decode outputs, so they belong with the renderer.

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

} // namespace dew::converter
