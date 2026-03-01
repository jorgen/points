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

#include <fmt/printf.h>

#include "renderer.hpp"

namespace points::converter
{

const frame_node_registry_t::node_set_t &draw_emitter_t::prepare_fade_outs(
  const std::vector<tree_walker_data_t> &walker_subsets)
{
  // Build current walker node set
  frame_node_registry_t::node_set_t walker_set;
  for (auto &ws : walker_subsets)
    walker_set.insert(ws.node);

  // Nodes in prev_active_set not in walker output → start fade-out
  for (auto &prev_id : m_prev_active_set)
  {
    if (walker_set.count(prev_id) == 0 && m_fading_out.count(prev_id) == 0)
      m_fading_out[prev_id] = 0;
  }

  // Remove fade-out entries that reappeared in walker output
  for (auto it = m_fading_out.begin(); it != m_fading_out.end();)
  {
    if (walker_set.count(it->first) > 0)
      it = m_fading_out.erase(it);
    else
      ++it;
  }

  // Build retain set from all fading-out nodes
  m_fade_out_retain.clear();
  for (auto &[node_id, _] : m_fading_out)
    m_fade_out_retain.insert(node_id);

  return m_fade_out_retain;
}

draw_result_t draw_emitter_t::emit(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                                   const frame_node_registry_t &registry,
                                   const selection_result_t &selection,
                                   render::callback_manager_t &callbacks,
                                   const render::frame_camera_cpp_t &camera,
                                   const tree_config_t &tree_config,
                                   render::to_render_t *to_render,
                                   bool debug)
{
  draw_result_t result;
  glm::dvec3 camera_position = glm::dvec3(camera.inverse_view[3]);

  // Distance-sort active nodes
  m_sorted_active.clear();
  m_sorted_active.reserve(selection.active_set.size());
  for (auto &node_id : selection.active_set)
  {
    auto *node = registry.get_node(node_id);
    if (!node)
      continue;
    glm::dvec3 center = (node->tight_aabb.min + node->tight_aabb.max) * 0.5;
    double dist = glm::length(center - camera_position);
    m_sorted_active.push_back({node_id, dist, node->frustum_visible});
  }
  std::sort(m_sorted_active.begin(), m_sorted_active.end(), [](const active_node_info_t &a, const active_node_info_t &b) { return a.distance < b.distance; });

  // Build set of node_ids still in any color transition (for top-down crossfade ordering).
  // Only consider active nodes — a parent swapped out by the selector is not being rendered
  // and must not block its children's crossfade.
  m_transitioning_nodes.clear();
  for (auto &node_id : selection.active_set)
  {
    auto *node = registry.get_node(node_id);
    if (!node)
      continue;
    for (int idx : node->buffer_indices)
    {
      auto &rb = *render_buffers[idx];
      if (rb.old_color_valid)
      {
        m_transitioning_nodes.insert(node_id);
        break;
      }
    }
  }

  // Emit draw commands — two passes over active nodes:
  //   Pass 1: steady-state (opaque, establishes depth buffer)
  //   Pass 2: transitioning (fading in, crossfading, awaiting — alpha blended)
  int dbg_emitted = 0, dbg_skipped_frustum = 0, dbg_skipped_unrendered = 0;
  int dbg_awaiting = 0, dbg_crossfading = 0, dbg_fading = 0, dbg_steady = 0;
  int dbg_fade_out = 0;
  int dbg_transition_logs = 0;

  // Helper lambda: classify whether a buffer is transitioning
  auto buffer_is_transitioning = [](const gpu_node_buffer_t &rb) {
    return rb.fade_frame < gpu_node_buffer_t::FADE_FRAMES || rb.old_color_valid || rb.awaiting_new_color;
  };

  // Pass 1: steady-state nodes (opaque, depth write)
  for (auto &info : m_sorted_active)
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
      if (buffer_is_transitioning(render_buffer))
        continue;

      auto offset = to_glm(tree_config.offset) + to_glm(render_buffer.offset);
      render_buffer.camera_view = camera.projection * glm::translate(camera.view, offset);
      callbacks.do_modify_buffer(render_buffer.render_buffers[2], 0, sizeof(render_buffer.camera_view), &render_buffer.camera_view);

      dbg_steady++;
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

      dbg_emitted++;
      result.points_rendered += render_buffer.point_count;
    }
  }

  // Pass 2: transitioning nodes (fading in, crossfading, awaiting)
  for (auto &info : m_sorted_active)
  {
    if (!info.frustum_visible)
    {
      dbg_skipped_frustum++;
      continue;
    }
    auto *node = registry.get_node(info.node_id);
    if (!node)
      continue;
    for (int idx : node->buffer_indices)
    {
      auto &render_buffer = *render_buffers[idx];
      if (!render_buffer.rendered)
      {
        dbg_skipped_unrendered++;
        continue;
      }
      if (!buffer_is_transitioning(render_buffer))
        continue;

      auto offset = to_glm(tree_config.offset) + to_glm(render_buffer.offset);
      render_buffer.camera_view = camera.projection * glm::translate(camera.view, offset);
      callbacks.do_modify_buffer(render_buffer.render_buffers[2], 0, sizeof(render_buffer.camera_view), &render_buffer.camera_view);

      bool is_crossfading = render_buffer.old_color_valid;
      bool is_awaiting = render_buffer.awaiting_new_color;

      result.any_animating = true;

      if (is_awaiting)
        dbg_awaiting++;
      else if (is_crossfading)
        dbg_crossfading++;
      else
        dbg_fading++;

      if (debug)
      {
        if (is_awaiting && !render_buffer.old_color_buffer.user_ptr)
          fmt::print(stderr, "[transition-debug] draw: WARNING null buffer ptr for node lod={} state=awaiting\n", render_buffer.node_info.lod);
        else if (is_crossfading)
        {
          if (!render_buffer.old_color_buffer.user_ptr)
            fmt::print(stderr, "[transition-debug] draw: WARNING null buffer ptr for node lod={} state=crossfading old_color\n", render_buffer.node_info.lod);
          if (!render_buffer.render_buffers[1].user_ptr)
            fmt::print(stderr, "[transition-debug] draw: WARNING null buffer ptr for node lod={} state=crossfading new_color\n", render_buffer.node_info.lod);
        }
        if (!render_buffer.render_buffers[0].user_ptr)
          fmt::print(stderr, "[transition-debug] draw: WARNING null buffer ptr for node lod={} state={} vertex\n",
                     render_buffer.node_info.lod, is_awaiting ? "awaiting" : is_crossfading ? "crossfading" : "fading");
      }

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
        bool parent_transitioning = m_transitioning_nodes.count(render_buffer.node_info.parent) > 0;
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
            result.freed_gpu_memory += render_buffer.old_color_memory;
            render_buffer.gpu_memory_size -= render_buffer.old_color_memory;
            render_buffer.old_color_memory = 0;
            is_crossfading = false;
          }
        }
      }

      render_buffer.params_data = glm::vec4(fade_alpha, blend, old_is_mono ? 1.0f : 0.0f, new_is_mono ? 1.0f : 0.0f);

      if (debug && dbg_transition_logs < 3)
      {
        const char *state = is_awaiting ? "awaiting" : is_crossfading ? "crossfading" : "fading";
        fmt::print(stderr, "[transition-debug] draw-buf: lod={} state={} fade_alpha={:.3f} blend={:.3f} old_mono={} new_mono={} color_ptr={} old_color_ptr={} vertex_ptr={}\n",
                   render_buffer.node_info.lod, state, fade_alpha, blend,
                   old_is_mono ? 1 : 0, new_is_mono ? 1 : 0,
                   render_buffer.render_buffers[1].user_ptr != nullptr,
                   render_buffer.old_color_buffer.user_ptr != nullptr,
                   render_buffer.render_buffers[0].user_ptr != nullptr);
        dbg_transition_logs++;
      }

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

      dbg_emitted++;
      result.points_rendered += render_buffer.point_count;
    }
  }

  // Pass 3: fade-out nodes (left active set, fading to transparent)
  for (auto it = m_fading_out.begin(); it != m_fading_out.end();)
  {
    auto *node = registry.get_node(it->first);
    bool remove = false;
    if (!node)
    {
      remove = true;
    }
    else
    {
      for (int idx : node->buffer_indices)
      {
        auto &render_buffer = *render_buffers[idx];
        if (!render_buffer.rendered)
          continue;

        float fade_alpha = 1.0f - float(it->second) / float(gpu_node_buffer_t::FADE_FRAMES);
        if (fade_alpha <= 0.0f)
        {
          remove = true;
          break;
        }

        auto offset = to_glm(tree_config.offset) + to_glm(render_buffer.offset);
        render_buffer.camera_view = camera.projection * glm::translate(camera.view, offset);
        callbacks.do_modify_buffer(render_buffer.render_buffers[2], 0, sizeof(render_buffer.camera_view), &render_buffer.camera_view);

        bool new_is_mono = (render_buffer.draw_type == render::dyn_points_1);
        render_buffer.params_data = glm::vec4(fade_alpha, 1.0f, 0.0f, new_is_mono ? 1.0f : 0.0f);

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
        render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
        render_buffer.render_list[1].user_ptr = render_buffer.render_buffers[1].user_ptr;
        render_buffer.render_list[2].buffer_mapping = render::dyn_points_bm_camera;
        render_buffer.render_list[2].user_ptr = render_buffer.render_buffers[2].user_ptr;
        render_buffer.render_list[3].buffer_mapping = render::dyn_points_bm_old_color;
        render_buffer.render_list[3].user_ptr = render_buffer.render_buffers[1].user_ptr;
        render_buffer.render_list[4].buffer_mapping = render::dyn_points_bm_params;
        render_buffer.render_list[4].user_ptr = render_buffer.params_buffer.user_ptr;

        render::draw_group_t draw_group = {render::dyn_points_crossfade, render_buffer.render_list, 5, int(render_buffer.point_count), render_buffer.node_info.lod};
        to_render_add_render_group(to_render, draw_group);

        dbg_emitted++;
        dbg_fade_out++;
        result.points_rendered += render_buffer.point_count;
      }
      result.any_animating = true;
      it->second++;
      if (it->second >= gpu_node_buffer_t::FADE_FRAMES)
        remove = true;
    }

    if (remove)
      it = m_fading_out.erase(it);
    else
      ++it;
  }

  // Force-complete crossfade for frustum-culled nodes — their crossfade counter
  // never advances since they're not drawn, so old_color_buffer would leak.
  int dbg_frustum_completed = 0;
  for (auto &info : m_sorted_active)
  {
    if (info.frustum_visible)
      continue;
    auto *node = registry.get_node(info.node_id);
    if (!node)
      continue;
    for (int idx : node->buffer_indices)
    {
      auto &rb = *render_buffers[idx];
      if (rb.old_color_valid && !rb.awaiting_new_color)
      {
        callbacks.do_destroy_buffer(rb.old_color_buffer);
        rb.old_color_valid = false;
        result.freed_gpu_memory += rb.old_color_memory;
        rb.gpu_memory_size -= rb.old_color_memory;
        rb.old_color_memory = 0;
        rb.crossfade_frame = 0;
        dbg_frustum_completed++;
      }
    }
  }

  // Save current active set for next frame's fade-out detection
  m_prev_active_set = selection.active_set;

  result.nodes_drawn = dbg_emitted;

  if (debug)
  {
    fmt::print(stderr, "[transition-debug] draw: emitted={} skipped_frustum={} skipped_unrendered={} awaiting={} crossfading={} fading={} steady={} fade_out={} frustum_completed={}\n",
               dbg_emitted, dbg_skipped_frustum, dbg_skipped_unrendered, dbg_awaiting, dbg_crossfading, dbg_fading, dbg_steady, dbg_fade_out, dbg_frustum_completed);
  }

  return result;
}

} // namespace points::converter
