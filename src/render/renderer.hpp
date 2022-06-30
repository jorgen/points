/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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

#include <points/render/renderer.h>

#include "renderer_callbacks.hpp"
#include "camera.hpp"
#include "data_source.hpp"
namespace points
{
namespace render
{

struct renderer_t
{
  renderer_t()
    : callbacks(this)
  {}
  std::vector<camera_t*> cameras;
  std::vector<data_source_t> data_sources;
  std::vector<draw_group_t> to_render;
  callback_manager_t callbacks;
};

}
}
