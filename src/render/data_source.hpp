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

#include <points/render/data_source.h>
#include <points/render/camera.h>
#include <points/render/renderer.h>
#include "glm_include.hpp"
#include <vector>

namespace points
{
namespace render
{

struct frame_camera_cpp_t
{
  glm::dmat4 view;
  glm::dmat4 projection;
  glm::dmat4 view_projection;
  glm::dmat4 inverse_view;
  glm::dmat4 inverse_projection;
  glm::dmat4 inverse_view_projection;
};

inline frame_camera_cpp_t cast_to_frame_camera_cpp(const frame_camera_t &camera)
{
  static_assert(sizeof(frame_camera_cpp_t) == sizeof(frame_camera_t), "Conversion types not the same size");
  frame_camera_cpp_t ret;
  memcpy(&ret, &camera, sizeof(ret));
  return ret;
}

struct data_source_cpp_t
{
  data_source_cpp_t()
  {
    data_source.user_ptr = this;
    data_source.add_to_frame = [](frame_camera_t *camera, to_render_t *to_render, void *user_ptr)
    {
      auto *thiz = static_cast<data_source_cpp_t *>(user_ptr);
      frame_camera_cpp_t c = cast_to_frame_camera_cpp(*camera);
      thiz->add_to_frame(c, to_render);
    };
  };
  virtual ~data_source_cpp_t() {}
  virtual void add_to_frame(const frame_camera_cpp_t &camera, to_render_t *to_render) = 0;
  data_source_t data_source;
};
} // namespace render
} // namespace points

