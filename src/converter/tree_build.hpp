/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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

// Octree CONSTRUCTION: routing incoming point chunks into the tree, subdividing nodes past the
// point limit, spawning sub-trees at level 4 and reparenting the root when new data falls outside
// it.
//
// Split out of tree.hpp/tree.cpp because construction is the only part of the tree that needs a
// storage_handler_t -- it reads point blobs back to split them across children. The remaining
// tree.{hpp,cpp} is pure format code (serialize/deserialize) with no storage dependency, which is
// what lets it move into the read/write core while this half stays with the write pipeline.

#include "tree.hpp"

namespace dew::converter
{
class storage_handler_t;

// Create a root tree sized to `header`'s morton span and insert its points.
tree_id_t tree_initialize(tree_registry_t &tree_registry, storage_handler_t &cache, const storage_header_t &header, attributes_id_t attributes_id, std::vector<storage_location_t> &&locations);

// Insert a chunk into an existing tree, subdividing and (if the chunk falls outside the current
// root) reparenting as needed. Returns the possibly-new root id.
tree_id_t tree_add_points(tree_registry_t &tree_registry, storage_handler_t &cache, const tree_id_t &tree_id, const storage_header_t &header, attributes_id_t attributes_id, std::vector<storage_location_t> &&locations);

} // namespace dew::converter
