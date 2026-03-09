/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jorgen Lind
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
//#include <points/render/renderer.h>
//#include <points/render/camera.h>

#include "renderer.hpp"

#include <vector>
#include <fmt/printf.h>

#include "camera.hpp"
#include "data_source.hpp"
#include "renderer_callbacks.hpp"

using namespace points::render;

struct points_renderer_t* points_renderer_create()
{
  auto renderer = new struct points_renderer_t();
  return renderer;
}
void points_renderer_destroy(struct points_renderer_t *renderer)
{
  delete renderer;
}
void points_renderer_add_camera(struct points_renderer_t* renderer, struct points_camera_t* camera)
{
  renderer->cameras.push_back(camera);
}
void points_renderer_remove_camera(struct points_renderer_t* renderer, struct points_camera_t* camera)
{
  auto& cams = renderer->cameras;
  cams.erase(std::remove(cams.begin(), cams.end(), camera), cams.end());
}

static void copy_to_array(double (&arr)[4][4], const glm::dmat4 &mat)
{
  static_assert(sizeof(mat) == sizeof(arr), "matrix sizes not the same");
  memcpy(&arr, &mat, sizeof(arr));
}

struct points_frame_t points_renderer_frame(struct points_renderer_t* renderer, struct points_camera_t* camera)
{
  points_frame_camera_t frame_camera;
  copy_to_array(frame_camera.view, camera->view);
  copy_to_array(frame_camera.projection, camera->projection);
  auto view_projection = camera->projection * camera->view;
  copy_to_array(frame_camera.view_projection, view_projection);
  copy_to_array(frame_camera.inverse_view, glm::inverse(camera->view));
  copy_to_array(frame_camera.inverse_projection, glm::inverse(camera->projection));
  copy_to_array(frame_camera.inverse_view_projection, glm::inverse(view_projection));
  renderer->to_render.clear();
  for (auto &data_source : renderer->data_sources)
  {
    data_source.add_to_frame(&frame_camera, reinterpret_cast<points_to_render_t *>(&renderer->to_render), data_source.user_ptr);
  }
  points_frame_t ret;
  ret.to_render = renderer->to_render.data();
  ret.to_render_size = int(renderer->to_render.size());
  return ret;
}

void points_renderer_set_callback(struct points_renderer_t* renderer, points_renderer_callbacks_t callbacks, void *user_ptr)
{
  renderer->callbacks.set_callbacks(callbacks, user_ptr);
}

void points_renderer_add_data_source(struct points_renderer_t *renderer, struct points_data_source_t data_source)
{
  auto it = std::find_if(renderer->data_sources.begin(), renderer->data_sources.end(), [&data_source](points_data_source_t &a)
  {
      return data_source.add_to_frame == a.add_to_frame && data_source.user_ptr == a.user_ptr;
  });
  if (it == renderer->data_sources.end())
    renderer->data_sources.push_back(data_source);
}

void points_renderer_remove_data_source(struct points_renderer_t* renderer, struct points_data_source_t data_source)
{
  auto it = std::find_if(renderer->data_sources.begin(), renderer->data_sources.end(), [&data_source](points_data_source_t &a)
  {
      return data_source.add_to_frame == a.add_to_frame && data_source.user_ptr == a.user_ptr;
  });
  if (it != renderer->data_sources.end())
    renderer->data_sources.erase(it);
}
void points_to_render_add_render_group(struct points_to_render_t *to_render, points_draw_group_t draw_group)
{
  auto *to_render_vec = reinterpret_cast<std::vector<points_draw_group_t> *>(to_render);
  to_render_vec->push_back(draw_group);
}
