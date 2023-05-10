/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2023  Jørgen Lind
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

#include <points/render/camera.h>
#include <points/render/renderer.h>
#include <points/render/data_source.h>
#include <points/converter/storage_data_source.h>

#include <points/render/draw_group.h>
#include "buffer.hpp"
#include "renderer_callbacks.hpp"
#include "converter.hpp"
#include "point_buffer_render_helper.h"

namespace points
{
namespace converter
{

struct current_storage_item_t
{
  uint32_t id;
  uint32_t sub;
  dyn_points_draw_buffer_t buffer;
};

struct storage_data_source_t
{
  storage_data_source_t(converter_t *converter, render::callback_manager_t &callback_manager);

  void add_to_frame(render::frame_camera_t *camera, render::to_render_t *to_render);
  void set_item(uint32_t id, uint32_t sub);
  int get_items(uint32_t **ids, uint32_t **subs, int buffer_size);
  int item_count();

  converter_t *converter;
  render::callback_manager_t &callbacks;
  render::data_source_t data_source;

  current_storage_item_t render_item;

  std::vector<uint32_t> ids;
  std::vector<uint32_t> subs;


  render::buffer_t project_view_buffer;
  render::buffer_t index_buffer;
};

}
}
