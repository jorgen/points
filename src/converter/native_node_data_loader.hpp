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

#include "conversion_types.hpp"
#include "frustum_tree_walker.hpp"
#include "node_data_loader.hpp"
#include "point_buffer_render_helper.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace points::converter
{

struct native_load_request_t
{
  point_format_t format[4];
  storage_location_t locations[4];
  tree_config_t tree_config;
};

struct pending_request_t
{
  std::shared_ptr<dyn_points_data_handler_t> data_handler;
  tree_config_t tree_config;
};

class native_node_data_loader_t final : public render::node_data_loader_t
{
public:
  explicit native_node_data_loader_t(storage_handler_t &storage_handler);

  render::load_handle_t request_load(const void *request_data, uint32_t request_size) override;
  bool is_ready(render::load_handle_t handle) override;
  render::loaded_node_data_t get_data(render::load_handle_t handle) override;
  void cancel(render::load_handle_t handle) override;

private:
  storage_handler_t &_storage_handler;
  std::mutex _mutex;
  std::atomic<uint64_t> _next_handle{1};
  std::unordered_map<uint64_t, pending_request_t> _pending;
};

} // namespace points::converter
