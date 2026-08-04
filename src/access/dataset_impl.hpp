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

#include <dew/access/query.h>

#include "attributes_configs.hpp"
#include "blob_reader.hpp"
#include "budget.hpp"
#include "decode.hpp"
#include "morton_tree_coordinate_transform.hpp"
#include "region_walk.hpp"
#include "tree.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dew::access
{
using namespace dew::core;

struct request_impl_t;

struct dataset_impl_t
{
  dataset_impl_t(std::string url, std::string connection, const dew_dataset_options_t &options);
  ~dataset_impl_t();

  void on_storage_error(const dew_error_t &&e);
  // Read and install one tree blob. Trees load lazily, so a walk reports what it needs and this
  // fetches it; installing only ever fills a slot the registry already sized at open.
  bool load_tree(tree_id_t id);
  bool walk_to_convergence(const region_query_t &query, region_result_t &out);
  void info(dew_dataset_info_t &out) const;

  std::string url;
  std::string connection;
  vio::thread_pool_t pool;
  vio::thread_with_event_loop_t loop_thread;
  vio::event_pipe_t<dew_error_t> storage_error;
  perf_stats_t perf;
  derived_budgets_t budgets;
  std::unique_ptr<blob_reader_t> reader;
  attributes_configs_t attributes;
  tree_registry_t registry;
  size_t registry_size_at_open = 0;

  dew_error_t error;
  std::atomic<dew_dataset_state_t> state{dew_dataset_opening};
  std::mutex mutex;
  std::vector<std::unique_ptr<request_impl_t>> requests;
};

// One attribute's concatenated output buffer.
struct out_buffer_t
{
  std::string name;
  dew_type_t type = dew_type_u8;
  dew_components_t components = dew_components_1;
  uint32_t stride = 0;
  std::vector<uint8_t> data;
};

struct request_impl_t
{
  void cancel();

  dataset_impl_t *dataset = nullptr;
  std::atomic<dew_request_status_t> status{dew_request_pending};
  dew_error_t error;
  dew_request_done_callback_t done = nullptr;
  void *done_user_ptr = nullptr;
  bool callback_fired = false;

  std::vector<out_buffer_t> buffers;
  std::vector<dew_result_node_t> nodes;
  uint64_t point_count = 0;

  // Views handed out by dew_request_get_result; kept alive by `buffers` until release.
  std::vector<dew_attribute_buffer_t> buffer_views;
};

} // namespace dew::access
