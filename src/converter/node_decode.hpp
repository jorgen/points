/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

// The offloadable, pure-CPU node decode. Given the decompressed blob buffers (decode_input_t) it produces the
// GPU-ready buffers (morton -> packed float3 vertices, attribute copy, coarse->fine LOD reorder + per-point
// rep_level). It has NO dependency on the storage handler, an event loop, the GPU, or the network -- so it can
// run on convert_pool (native), inline (single-threaded wasm today), or inside a decode Web Worker fed raw
// bytes over postMessage. This header is the clean seam to extract for that worker.

#include "dataset_types.hpp"           // tree_config_t
#include "frustum_tree_walker.hpp"        // tree_walker_data_t (referenced by dyn_points_draw_buffer_t)
#include "node_data_loader.hpp"           // render::loaded_node_data_t
#include "point_buffer_render_helper.hpp" // decode_input_t, dyn_points_data_handler_t, convert_* helpers

#include <memory>

namespace dew::converter
{

// Backing store kept alive by loaded_node_data_t::_impl_data: it owns the decoded buffers the result's raw
// pointers point into, plus (optionally) the source data_handler so a promoted spanning leaf can recover its
// pre-reorder morton codes for virtual subdivision.
struct loaded_node_impl_data_t
{
  std::shared_ptr<dyn_points_data_handler_t> data_handler;
  std::shared_ptr<uint8_t[]> vertex_data;
  std::shared_ptr<uint8_t[]> attribute_data;
  std::shared_ptr<uint8_t[]> rep_level_data;
};

// Decode one node's (decompressed) buffers into GPU-ready data. `salvage_handler`, if given, is stashed on the
// result for later virtual-subdivision (pass nullptr when there is no handler, e.g. a decode worker that only
// has the raw bytes). Pure CPU; safe to call from any thread/worker.
render::loaded_node_data_t decode_node(const decode_input_t &in, const tree_config_t &tree_config,
                                       std::shared_ptr<dyn_points_data_handler_t> salvage_handler = nullptr);

} // namespace dew::converter
