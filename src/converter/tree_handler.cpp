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

#include <fmt/printf.h>

namespace points
{
namespace converter
{
tree_handler_t::tree_handler_t(const tree_global_state_t &global_state, cache_file_handler_t &file_cache, event_pipe_t<input_data_id_t> &done_with_input)
  : _initialized(false)
  , _global_state(global_state)
  , _file_cache(file_cache)
  , _tree_lod_generator(_event_loop, _tree_cache, _file_cache)
  , _add_points(_event_loop, [this](std::vector<internal_header_t> &&events){this->handle_add_points(std::move(events));})
  , _done_with_input(done_with_input)
{
  _event_loop.add_about_to_block_listener(this);
  _tree_cache.current_id = 0;
  _tree_cache.current_lod_node_id = uint64_t(1) << 63;
}

void tree_handler_t::about_to_block()
{
}

void tree_handler_t::add_points(internal_header_t &&header)
{
  _add_points.post_event(std::move(header));
}

void tree_handler_t::handle_add_points(std::vector<internal_header_t> &&events)
{
  for (auto &event : events)
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
}

void tree_handler_t::generate_lod(morton::morton192_t &min)
{
 _tree_lod_generator.generate_lods(_tree_root, min);
}

}
}
