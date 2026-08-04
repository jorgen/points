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

#include "dataset_impl.hpp"

#include "budget.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace dew::access
{

dataset_impl_t::dataset_impl_t(std::string a_url, std::string a_connection, const dew_dataset_options_t &options)
  : url(std::move(a_url))
  , connection(std::move(a_connection))
  , pool(options.decode_threads ? options.decode_threads : std::max(2u, std::thread::hardware_concurrency() / 2))
  , storage_error(loop_thread.event_loop(), vio::event_bind_t::bind(*this, &dataset_impl_t::on_storage_error))
{
  const uint64_t budget = options.memory_budget_bytes ? options.memory_budget_bytes : (uint64_t(512) << 20);
  budgets = derive_budgets(budget);

  reader = std::make_unique<blob_reader_t>(url, pool, perf, storage_error, error);
  if (error.code != 0)
  {
    state.store(dew_dataset_error, std::memory_order_release);
    return;
  }
  reader->set_read_cache_size(budgets.read_cache_bytes);
  reader->set_decompressed_cache_size(budgets.decompressed_cache_bytes);

  if (!reader->file_exists())
  {
    error = {1, "dataset does not exist: " + reader->file_exists_error()};
    state.store(dew_dataset_error, std::memory_order_release);
    return;
  }

  // Index + registry bootstrap. read_index is synchronous on the storage loop; the async open the
  // design calls for needs storage_backend_t to expose its already-existing coroutine form, which is
  // a change to the backend interface rather than to this file. Native callers see no difference
  // (they would wait_ready anyway); the wasm shim is what will need it.
  index_load_t load;
  error = reader->read_index(load);
  if (error.code != 0)
  {
    state.store(dew_dataset_error, std::memory_order_release);
    return;
  }
  if (load.attribute_configs && load.attribute_configs_size > 0)
  {
    error = attributes.deserialize(load.attribute_configs, load.attribute_configs_size);
    if (error.code != 0)
    {
      state.store(dew_dataset_error, std::memory_order_release);
      return;
    }
  }
  if (!load.tree_registry || load.tree_registry_size == 0)
  {
    error = {1, "dataset has no tree registry"};
    state.store(dew_dataset_error, std::memory_order_release);
    return;
  }
  error = tree_registry_deserialize(load.tree_registry, load.tree_registry_size, registry);
  if (error.code != 0)
  {
    state.store(dew_dataset_error, std::memory_order_release);
    return;
  }
  // The registry is fully sized here and the read path never grows it. Every lock-free reader below
  // -- and the synchronous node accessors -- depend on that invariant holding.
  registry_size_at_open = registry.data.size();

  if (!load_tree(registry.root))
  {
    state.store(dew_dataset_error, std::memory_order_release);
    return;
  }
  state.store(dew_dataset_ready, std::memory_order_release);
}

dataset_impl_t::~dataset_impl_t()
{
  // Cancel and drop every outstanding request before the reader (and its loop) goes away: a pending
  // request holds read_request_t objects that the loop is still completing.
  for (auto &request : requests)
    request->cancel();
  requests.clear();
  pool.join();
  if (reader)
    reader->stop_loop();
}

void dataset_impl_t::on_storage_error(const dew_error_t &&e)
{
  std::unique_lock<std::mutex> lock(mutex);
  if (error.code == 0)
    error = e;
}

bool dataset_impl_t::load_tree(tree_id_t id)
{
  if (id.data >= registry.locations.size())
    return false;
  if (id.data < registry.tree_id_initialized.size() && registry.tree_id_initialized[id.data])
    return true;
  const auto location = registry.locations[id.data];
  if (location.size == 0)
    return false;

  auto request = reader->read(location, read_options_t{false, true, {}});
  request->wait_for_read();
  if (request->error.code != 0)
  {
    error = request->error;
    return false;
  }
  serialized_tree_t serialized;
  serialized.size = int(request->buffer_info.size);
  serialized.data = request->buffer;

  auto tree = std::make_unique<tree_t>();
  dew_error_t tree_error;
  if (!tree_deserialize(serialized, *tree, tree_error))
  {
    error = tree_error;
    return false;
  }
  tree_compute_leaves_collapsed(*tree, registry);
  registry.data[id.data] = std::move(tree);
  registry.tree_id_initialized[id.data] = 1;
  return true;
}

// Walk, load whatever sub-trees the walk asked for, walk again. The walk itself never reads storage,
// so this loop is the only place a region query blocks.
bool dataset_impl_t::walk_to_convergence(const region_query_t &query, region_result_t &out)
{
  constexpr int max_rounds = 64;
  for (int round = 0; round < max_rounds; round++)
  {
    region_walk(registry, query, out);
    if (out.trees_to_load.empty())
      return true;
    for (auto id : out.trees_to_load)
    {
      if (!load_tree(id))
        return false;
    }
  }
  error = {1, "region walk did not converge"};
  return false;
}

void dataset_impl_t::info(dew_dataset_info_t &out) const
{
  memset(&out, 0, sizeof(out));
  out.scale = registry.tree_config.scale;
  for (int i = 0; i < 3; i++)
    out.offset[i] = registry.tree_config.offset[i];
  out.node_point_limit = registry.tree_config.node_point_limit;
  out.attribute_count = attributes.attrib_name_registry_count();

  if (registry.root.data < registry.tree_id_initialized.size() && registry.tree_id_initialized[registry.root.data])
  {
    const auto *root = registry.get(registry.root);
    if (root)
    {
      double lo[3];
      double hi[3];
      convert_morton_to_pos(registry.tree_config.scale, registry.tree_config.offset, root->morton_min, lo);
      convert_morton_to_pos(registry.tree_config.scale, registry.tree_config.offset, root->morton_max, hi);
      for (int i = 0; i < 3; i++)
      {
        out.aabb_min[i] = lo[i];
        out.aabb_max[i] = hi[i];
      }
      out.root_lod = morton::morton_magnitude_to_lod(root->magnitude);
      out.deepest_lod = out.root_lod;
      if (!root->data[0].empty())
        out.point_count = root->data[0][0].point_count;
    }
  }
  out.point_count_exact = 0; // no total is persisted; only a full enumeration could make this exact
}

} // namespace dew::access
