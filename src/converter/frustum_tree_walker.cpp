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
#include "frustum_tree_walker.hpp"

namespace points
{
namespace converter
{
frustum_tree_walker_t::frustum_tree_walker_t()
{

}

void frustum_tree_walker_t::walk_tree(tree_cache_t tree_cache, tree_id_t tree_root, cache_file_handler_t &file_cache)
{
  (void) tree_cache;
  (void) tree_root;
  (void) file_cache;
  //auto root_tree = tree_cache.get(tree_root);
  //root_tree->data
}

}
}
