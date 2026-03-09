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

#include <points/common/format.h>
#include <points/render/draw_group.h>

#include <array>
#include <cstdint>
#include <memory>

namespace points::render
{

using load_handle_t = uint64_t;
constexpr load_handle_t invalid_load_handle = 0;

struct loaded_node_data_t
{
  void *vertex_data = nullptr;
  uint32_t vertex_data_size = 0;
  points_type_t vertex_type = points_type_r32;
  points_components_t vertex_components = points_components_3;

  void *attribute_data = nullptr;
  uint32_t attribute_data_size = 0;
  points_type_t attribute_type = points_type_u8;
  points_components_t attribute_components = points_components_3;

  uint32_t point_count = 0;
  std::array<double, 3> offset = {};
  points_draw_type_t draw_type = points_dyn_points_1;

  std::shared_ptr<void> _impl_data;

  void release()
  {
    _impl_data.reset();
    vertex_data = nullptr;
    attribute_data = nullptr;
  }
};

class node_data_loader_t
{
public:
  virtual ~node_data_loader_t() = default;
  virtual load_handle_t request_load(const void *request_data, uint32_t request_size) = 0;
  virtual bool is_ready(load_handle_t handle) = 0;
  virtual loaded_node_data_t get_data(load_handle_t handle) = 0;
  virtual void cancel(load_handle_t handle) = 0;
};

} // namespace points::render
