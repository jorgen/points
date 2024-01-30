/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#include "tree_handler.hpp"

#include "tree_lod_generator.hpp"
#include "frustum_tree_walker.hpp"

#include <fmt/printf.h>

namespace points
{
namespace converter
{
tree_handler_t::tree_handler_t(const tree_global_state_t &global_state, cache_file_handler_t &file_cache, attributes_configs_t &attributes_configs, event_pipe_single_t<input_data_id_t> &done_with_input)
  : _initialized(false)
  , _global_state(global_state)
  , _file_cache(file_cache)
  , _attributes_configs(attributes_configs)
  , _tree_lod_generator(_event_loop, global_state, _tree_cache, _file_cache, _attributes_configs)
  , _add_points(_event_loop, bind(&tree_handler_t::handle_add_points))
  , _walk_tree(_event_loop, bind(&tree_handler_t::handle_walk_tree))
  , _done_with_input(done_with_input)
{
  _event_loop.add_about_to_block_listener(this);
  _tree_cache.current_id = 0;
  _tree_cache.current_lod_node_id = uint64_t(1) << 63;
}

void tree_handler_t::about_to_block()
{
}

void tree_handler_t::add_points(storage_header_t &&header)
{
  _add_points.post_event(std::move(header));
}

void tree_handler_t::walk_tree(const std::shared_ptr<frustum_tree_walker_t> &event)
{
  _walk_tree.post_event(event);
}
void tree_handler_t::handle_add_points(storage_header_t &&event)
{
  if (!_initialized)
  {
    _initialized = true;
    _tree_root = tree_initialize(_global_state, _tree_cache, _file_cache, event);
  }
  else
  {
    _tree_root = tree_add_points(_global_state, _tree_cache, _file_cache, _tree_root, event);
  }
  _done_with_input.post_event(event.input_id);
}

void tree_handler_t::handle_walk_tree(std::shared_ptr<frustum_tree_walker_t> &&event)
{
  event->walk_tree(_global_state, _tree_cache, _tree_root);
}

void tree_handler_t::generate_lod(const morton::morton192_t &max)
{
 _tree_lod_generator.generate_lods(_tree_root, max);
}

}
}
