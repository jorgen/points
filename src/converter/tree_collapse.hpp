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

// Leaf collapse at finality: leaves reference contiguous SUBSETS of big morton-sorted ingest
// chunks while a tree is building; once a subtree can never receive points again (its morton_max
// is below the pass watermark), each leaf's subsets are merged into the leaf's own storage unit
// (<= node_point_limit points, one compressed blob per attribute) and the chunk units are
// discarded through the checkpoint freed machinery when their last referencing tree collapses.
// After collapse the cache -- and therefore the bucket, which uploads units verbatim as one
// object per blob -- is per-node: a renderer downloads exactly a node's blobs, whole objects.
//
// Runs at the START of each LOD pass, before LOD generation: the affected leaves are already
// immutable, and level-5 LOD sampling then reads the small per-node units instead of
// re-decompressing whole chunks per node. Sequencing: collapse -> LOD -> checkpoint (finality is
// gated on tree_t::leaves_collapsed) -> band upload.

#include "tree.hpp"
#include "tree_lod_generator.hpp" // child_storage_map_t + shared quantize/slice helpers

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#include <vio/thread_pool.h>

#include <atomic>
#include <functional>
#include <vector>

namespace dew::converter
{
class storage_handler_t;
class attributes_configs_t;

struct collapse_job_t
{
  tree_id_t tree_id;
  int level;
  uint32_t node_index; // index into tree->data[level]
  input_data_id_t new_id;
  points_collection_t collection;  // snapshot of the leaf's subsets (the leaf is immutable)
  child_storage_map_t sources;     // unit id -> {attributes_id, locations}
  // Worker outputs:
  attributes_id_t generated_attributes_id = {};
  std::vector<storage_location_t> generated_locations;
  morton::morton192_t generated_min = {};
  morton::morton192_t generated_max = {};
  bool failed = false;
  bool applied = false; // set by apply_results after the tree consumed the outputs
};

class tree_collapse_runner_t
{
public:
  tree_collapse_runner_t(vio::event_loop_t &event_loop, vio::thread_pool_t &thread_pool, tree_registry_t &tree_registry, storage_handler_t &storage, attributes_configs_t &attributes_configs);

  // Tree loop only; one pass at a time. Enumerates every loaded building tree that the pass
  // target proves final-able, merges its uncollapsed leaves on the thread pool, applies the
  // results on the tree loop (rewriting collections, releasing chunk references) and then calls
  // on_done exactly once -- synchronously when there is nothing to collapse.
  void collapse_for_pass(const morton::morton192_t &target, std::function<void()> on_done);

  void merge_worker(collapse_job_t &job); // pool thread

private:
  void handle_worker_done();
  void apply_results();

  vio::event_loop_t &_event_loop;
  vio::thread_pool_t &_thread_pool;
  tree_registry_t &_tree_registry;
  storage_handler_t &_storage;
  attributes_configs_t &_attributes_configs;

  vio::event_pipe_t<void> _worker_done;

  std::vector<collapse_job_t> _jobs;
  std::atomic<uint32_t> _completed = 0;
  std::function<void()> _on_done;
};

} // namespace dew::converter
