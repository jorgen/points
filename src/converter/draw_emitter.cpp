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
#include "draw_emitter.hpp"

#include <algorithm>
#include <unordered_set>

#include "renderer.hpp"

namespace points::converter
{

draw_result_t draw_emitter_t::emit(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                                   const frame_node_registry_t &registry,
                                   const selection_result_t &selection,
                                   render::callback_manager_t &callbacks,
                                   const render::frame_camera_cpp_t &camera,
                                   const tree_config_t &tree_config,
                                   render::to_render_t *to_render)
{
  draw_result_t result;
  glm::dvec3 camera_position = glm::dvec3(camera.inverse_view[3]);

  // Distance-sort active nodes
  struct active_node_info_t
  {
    node_id_t node_id;
    double distance;
    bool frustum_visible;
  };
  std::vector<active_node_info_t> sorted_active;
  sorted_active.reserve(selection.active_set.size());
  for (auto &node_id : selection.active_set)
  {
    auto *node = registry.get_node(node_id);
    if (!node)
      continue;
    glm::dvec3 center = (node->tight_aabb.min + node->tight_aabb.max) * 0.5;
    double dist = glm::length(center - camera_position);
    sorted_active.push_back({node_id, dist, node->frustum_visible});
  }
  std::sort(sorted_active.begin(), sorted_active.end(), [](const active_node_info_t &a, const active_node_info_t &b) { return a.distance < b.distance; });

  // Build set of node_ids still in any color transition (for top-down crossfade ordering).
  // Only consider active nodes — a parent swapped out by the selector is not being rendered
  // and must not block its children's crossfade.
  frame_node_registry_t::node_set_t transitioning_nodes;
  for (auto &node_id : selection.active_set)
  {
    auto *node = registry.get_node(node_id);
    if (!node)
      continue;
    for (int idx : node->buffer_indices)
    {
      auto &rb = *render_buffers[idx];
      if (rb.old_color_valid || rb.awaiting_new_color)
      {
        transitioning_nodes.insert(node_id);
        break;
      }
    }
  }

  // Emit draw commands
  for (auto &info : sorted_active)
  {
    if (!info.frustum_visible)
      continue;
    auto *node = registry.get_node(info.node_id);
    if (!node)
      continue;
    for (int idx : node->buffer_indices)
    {
      auto &render_buffer = *render_buffers[idx];
      if (!render_buffer.rendered)
        continue;

      auto offset = to_glm(tree_config.offset) + to_glm(render_buffer.offset);
      render_buffer.camera_view = camera.projection * glm::translate(camera.view, offset);
      callbacks.do_modify_buffer(render_buffer.render_buffers[2], 0, sizeof(render_buffer.camera_view), &render_buffer.camera_view);

      bool is_fading = render_buffer.fade_frame < gpu_node_buffer_t::FADE_FRAMES;
      bool is_crossfading = render_buffer.old_color_valid;
      bool is_awaiting = render_buffer.awaiting_new_color;

      if (is_fading || is_crossfading || is_awaiting)
      {
        result.any_animating = true;

        render_buffer.fade_frame++;
        float fade_alpha = std::min(1.0f, float(render_buffer.fade_frame) / float(gpu_node_buffer_t::FADE_FRAMES));

        float blend = 1.0f;
        bool old_is_mono = false;
        bool new_is_mono = (render_buffer.draw_type == render::dyn_points_1);

        if (is_awaiting)
        {
          blend = 1.0f;
          new_is_mono = render_buffer.old_color_is_mono;
        }
        else if (is_crossfading)
        {
          old_is_mono = render_buffer.old_color_is_mono;
          bool parent_transitioning = transitioning_nodes.count(render_buffer.node_info.parent) > 0;
          if (parent_transitioning)
          {
            blend = 0.0f;
          }
          else
          {
            render_buffer.crossfade_frame++;
            blend = std::min(1.0f, float(render_buffer.crossfade_frame) / float(gpu_node_buffer_t::CROSSFADE_FRAMES));
            if (blend >= 1.0f)
            {
              callbacks.do_destroy_buffer(render_buffer.old_color_buffer);
              render_buffer.old_color_valid = false;
              is_crossfading = false;
            }
          }
        }

        render_buffer.params_data = glm::vec4(fade_alpha, blend, old_is_mono ? 1.0f : 0.0f, new_is_mono ? 1.0f : 0.0f);
        if (!render_buffer.params_buffer.user_ptr)
        {
          callbacks.do_create_buffer(render_buffer.params_buffer, points::render::buffer_type_uniform);
          callbacks.do_initialize_buffer(render_buffer.params_buffer, type_r32, points::components_4, sizeof(render_buffer.params_data), &render_buffer.params_data);
        }
        else
        {
          callbacks.do_modify_buffer(render_buffer.params_buffer, 0, sizeof(render_buffer.params_data), &render_buffer.params_data);
        }

        render_buffer.render_list[0].buffer_mapping = render::dyn_points_bm_vertex;
        render_buffer.render_list[0].user_ptr = render_buffer.render_buffers[0].user_ptr;
        render_buffer.render_list[2].buffer_mapping = render::dyn_points_bm_camera;
        render_buffer.render_list[2].user_ptr = render_buffer.render_buffers[2].user_ptr;

        if (is_awaiting)
        {
          render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
          render_buffer.render_list[1].user_ptr = render_buffer.old_color_buffer.user_ptr;
          render_buffer.render_list[3].buffer_mapping = render::dyn_points_bm_old_color;
          render_buffer.render_list[3].user_ptr = render_buffer.old_color_buffer.user_ptr;
        }
        else if (is_crossfading)
        {
          render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
          render_buffer.render_list[1].user_ptr = render_buffer.render_buffers[1].user_ptr;
          render_buffer.render_list[3].buffer_mapping = render::dyn_points_bm_old_color;
          render_buffer.render_list[3].user_ptr = render_buffer.old_color_buffer.user_ptr;
        }
        else
        {
          render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
          render_buffer.render_list[1].user_ptr = render_buffer.render_buffers[1].user_ptr;
          render_buffer.render_list[3].buffer_mapping = render::dyn_points_bm_old_color;
          render_buffer.render_list[3].user_ptr = render_buffer.render_buffers[1].user_ptr;
        }

        render_buffer.render_list[4].buffer_mapping = render::dyn_points_bm_params;
        render_buffer.render_list[4].user_ptr = render_buffer.params_buffer.user_ptr;

        render::draw_group_t draw_group = {render::dyn_points_crossfade, render_buffer.render_list, 5, int(render_buffer.point_count), render_buffer.node_info.lod};
        to_render_add_render_group(to_render, draw_group);
      }
      else
      {
        // Steady state
        if (render_buffer.params_buffer.user_ptr)
          callbacks.do_destroy_buffer(render_buffer.params_buffer);

        render_buffer.render_list[0].buffer_mapping = render::dyn_points_bm_vertex;
        render_buffer.render_list[0].user_ptr = render_buffer.render_buffers[0].user_ptr;
        render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
        render_buffer.render_list[1].user_ptr = render_buffer.render_buffers[1].user_ptr;
        render_buffer.render_list[2].buffer_mapping = render::dyn_points_bm_camera;
        render_buffer.render_list[2].user_ptr = render_buffer.render_buffers[2].user_ptr;

        render::draw_group_t draw_group = {render_buffer.draw_type, render_buffer.render_list, 3, int(render_buffer.point_count), render_buffer.node_info.lod};
        to_render_add_render_group(to_render, draw_group);
      }
      result.points_rendered += render_buffer.point_count;
    }
  }

  return result;
}

} // namespace points::converter
