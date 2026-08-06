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

// The resident set of octree trees: which ones are in memory, and how to get the rest.
//
// A converted dataset's octree is split across many tree_t blobs, and a reader only ever wants a few
// of them -- the ones a walk actually descends into. So the registry is sized once at open (locations
// for every tree, data for none) and trees are read in on demand.
//
// Two consumers need exactly this, and until now each had its own copy:
//
//   * a QUERY wants a converged answer, so it awaits the load and re-walks until nothing is missing;
//   * a RENDERER must answer every frame, so it draws what is resident, asks for the rest, and picks
//     it up on a later frame.
//
// Those are different WAITS, not different loaders -- the read, the deserialize, the collapse pass
// and the install are identical, and are the fiddly part. Both shapes live here over one
// implementation, so a fix to the install path cannot land on only one of them.
//
// THREADING. All the bookkeeping happens on the event loop handed to the constructor: install()
// mutates the registry, and the dedupe state goes with it. request() is callable from any thread
// because it POSTS to that loop; load() is a coroutine and must already be running on it.
// resident() is safe anywhere -- tree_id_initialized is published with a release store, since a
// renderer walks the registry from its own thread while loads land on the loop.

#include "blob_reader.hpp"
#include "tree.hpp"

#include <dew/core/error.h>

#include <vio/event_loop.h>
#include <vio/task.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace dew::core
{

class tree_set_t
{
public:
  // `reader` and `loop` must outlive this. `loop` is where loads run and where the registry is
  // mutated -- give it the loop that also does the walking.
  tree_set_t(blob_reader_t &reader, vio::event_loop_t &loop);

  tree_set_t(const tree_set_t &) = delete;
  tree_set_t &operator=(const tree_set_t &) = delete;

  // Size the registry from the serialized registry blob (locations for every tree, data for none).
  // Everything else here is meaningless until this succeeds.
  [[nodiscard]] dew_error_t initialize(const std::unique_ptr<uint8_t[]> &data, uint32_t size);

  [[nodiscard]] const tree_registry_t &registry() const { return _registry; }
  // Non-const access, for a walker that takes the registry by reference. Mutating it from outside is
  // the caller's problem; nothing here defends against it.
  [[nodiscard]] tree_registry_t &registry() { return _registry; }

  [[nodiscard]] tree_id_t root() const { return _registry.root; }
  [[nodiscard]] tree_config_t tree_config() const { return _registry.tree_config; }
  [[nodiscard]] bool resident(tree_id_t id) const;

  // AWAIT the tree. Resumes on `loop` once it is resident, or false with `error` set. For a caller
  // that wants a complete answer before it replies -- a query.
  vio::task_t<bool> load(tree_id_t id, dew_error_t &error);

  // ASK for the trees and return immediately. For a caller that must answer now and can pick the
  // result up later -- a renderer, which re-walks next frame. Deduplicated: a tree already resident
  // or already in flight is skipped, so calling this every frame with the same walk output costs
  // nothing after the first.
  //
  // Safe from ANY thread, and that is the reason it posts to the loop rather than acting inline: a
  // renderer walks on its own thread, so doing the dedupe bookkeeping in the caller would race the
  // loop that finishes the loads. The work is therefore queued, not done -- in_flight() does not
  // change until the loop picks it up. (tree_handler_t::request_trees_async posts for the same
  // reason.)
  void request(std::vector<tree_id_t> ids);

  // Stop starting new loads. Call before tearing down the loop; in-flight loads still complete.
  void begin_shutdown() { _shutting_down = true; }

  // How many requested loads have not finished. A frame-driven consumer can use this to decide
  // whether another frame is worth scheduling.
  [[nodiscard]] uint32_t in_flight() const { return _in_flight.load(std::memory_order_acquire); }
  // Loads ever STARTED. Monotonic, so it says how many reads the dedupe let through regardless of
  // when they finished -- which is the only timing-independent way to observe that repeats were
  // dropped rather than merely completed before the repeat arrived.
  [[nodiscard]] uint32_t loads_started() const { return _loads_started.load(std::memory_order_acquire); }

  // The bounds of the DATA, scanned from the root tree's point collections.
  //
  // Deliberately NOT the root octree cell, which is a power-of-two cube that can be far larger. This
  // is what a renderer wants in order to frame the camera on the points. It is derived from morton
  // min/max, so treat it as a framing hint rather than an exact extent -- the smallest morton code in
  // a set is not the per-axis minimum. dew_dataset_get_info reports the CELL instead, and says why.
  void data_aabb(double min[3], double max[3]) const;

private:
  // The loop-side half of request(): dedupe and spawn. Never called directly from another thread.
  void start_requested(const std::vector<tree_id_t> &ids);
  // Read the blob for `id`. Shared by both wait shapes.
  vio::task_t<bool> do_load(tree_id_t id, dew_error_t &error);
  // Deserialize and install into the registry slot. The part that must not be written twice.
  bool install(tree_id_t id, const serialized_tree_t &data, dew_error_t &error);

  blob_reader_t &_reader;
  vio::event_loop_t &_loop;
  tree_registry_t _registry;
  // Per-tree "a load has been started", so request() can be called every frame without piling up
  // duplicate reads. Distinct from tree_id_initialized, which means "the data is here".
  std::vector<uint8_t> _requested;
  // Atomic because both are public observations and a renderer reads them off its own thread.
  std::atomic<uint32_t> _in_flight{0};
  std::atomic<uint32_t> _loads_started{0};
  bool _shutting_down = false;
};

} // namespace dew::core
