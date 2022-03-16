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
#pragma once

#include "threaded_event_loop.hpp"
#include "tree.hpp"

namespace points
{
namespace converter
{
class tree_handler_t : public about_to_block_t
{
public:
  tree_handler_t(const tree_global_state_t &global_state, cache_file_handler_t &cache, event_pipe_t<input_data_id_t> &done_input);

  void about_to_block() override;

  void add_points(internal_header_t &&header);
private:
  void handle_add_points(std::vector<internal_header_t> &&events);
  threaded_event_loop_t _event_loop;
  bool _initialized;

  const tree_global_state_t &_global_state;
  cache_file_handler_t &_file_cache;

  tree_cache_t _tree_cache;
  tree_id_t _tree_root;

  event_pipe_t<internal_header_t> _add_points;
  event_pipe_t<input_data_id_t> &_done_with_input;
};

}
}