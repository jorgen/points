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
#include "input_data_source_registry.hpp"

#include "morton_tree_coordinate_transform.hpp"

namespace points::converter
{
static auto &get_item(input_data_id_t id, ankerl::unordered_dense::map<uint32_t, input_data_source_impl_t> &registry)
{
  assert(id.data < uint32_t(1) << 31);
  assert(registry.contains(id.data));
  return registry[id.data];
}

input_data_source_registry_t::input_data_source_registry_t()
  : _input_data_with_sub_parts(0)
  , _input_data_inserted_to_tree(0)
  , _unsorted_input_sources_dirty(true)
{
}
input_data_source_registry_t::~input_data_source_registry_t()
{
}

input_data_id_t get_next_input_id()
{
  static uint32_t next_input_id = 0;
  input_data_id_t ret;
  ret.data = next_input_id++;
  ret.sub = 0;
  return ret;
}

input_data_reference_t input_data_source_registry_t::register_file(std::unique_ptr<char[]> &&name, uint32_t name_length)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto input_id = get_next_input_id();
  auto &item = _registry[input_id.data];
  item.input_id = input_id;
  item.name = std::move(name);
  item.name_length = name_length;
  item.attribute_id = {0};
  item.public_header = {};
  return {input_id, {item.name.get(), item.name_length}};
}
  
void input_data_source_registry_t::register_pre_init_result(const tree_global_state_t &global_state, input_data_id_t id, bool found_min, double(&min)[3], uint64_t approximate_point_count, uint8_t approximate_point_size_bytes)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &item = get_item(id, _registry);
  item.approximate_point_count = approximate_point_count;
  item.approximate_point_size_bytes = approximate_point_size_bytes;
  if (found_min)
    convert_pos_to_morton(global_state.scale, global_state.offset, min, item.input_order);
  else
    memset(&item.input_order, 0, sizeof(item.input_order));
  _unsorted_input_sources_dirty = true;

}

void input_data_source_registry_t::handle_input_init(input_data_id_t id, attributes_id_t attributes_id, header_t public_header)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &item = get_item(id, _registry);
  item.attribute_id = attributes_id;
  item.public_header = public_header;
}

void input_data_source_registry_t::handle_sub_added(input_data_id_t id)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &item = get_item(id, _registry);
  item.sub_count++;
  _input_data_with_sub_parts++; 
}

void input_data_source_registry_t::handle_sorted_points(input_data_id_t id, const morton::morton192_t& min, const morton::morton192_t& max)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &item = get_item(id, _registry);

  if (min < item.morton_min)
    item.morton_min = min;
  if (max > item.morton_max)
    item.morton_max = max;
}

void input_data_source_registry_t::handle_points_written(input_data_id_t id, std::vector<storage_location_t> && location)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &item = get_item(id, _registry);
  if (id.sub >= item.storage_locations.size())
  {
    item.storage_locations.resize(item.sub_count);
  }
  item.storage_locations = std::move(location);
}

void input_data_source_registry_t::handle_reading_done(input_data_id_t id)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &item = get_item(id, _registry);
  item.read_finished = true;
}
  
void input_data_source_registry_t::handle_tree_done_with_input(input_data_id_t id)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &item = get_item(id, _registry);
  item.inserted_into_tree++;
  _input_data_inserted_to_tree++;
}
  
bool input_data_source_registry_t::all_inserted_into_tree() const
{
  std::unique_lock<std::mutex> lock(_mutex);
  return _input_data_with_sub_parts == _input_data_inserted_to_tree;
}

std::optional<input_data_next_input_t> input_data_source_registry_t::next_input_to_process()
{
  std::unique_lock<std::mutex> lock(_mutex);
  if (_unsorted_input_sources_dirty)
  {
    _unsorted_input_sources_dirty = false;
    _unsorted_input_sources = {};
    for (auto &item : _registry)
    {
      if (!item.second.read_started)
      {
        _unsorted_input_sources.push_back(item.first);
      }
    }
    std::make_heap(_unsorted_input_sources.begin(), _unsorted_input_sources.end(), [&](uint32_t a, uint32_t b) { return _registry[a].input_order > _registry[b].input_order; });
  }
  if (_unsorted_input_sources.empty())
    return {};

  std::pop_heap(_unsorted_input_sources.begin(), _unsorted_input_sources.end());
  auto id = _unsorted_input_sources.back();
  _unsorted_input_sources.pop_back();
  _sorted_input_sources.push_back(id);
  auto &item = _registry[id];
  input_data_next_input_t ret;
  ret.approximate_point_count = item.approximate_point_count;
  ret.approximate_point_size_bytes = item.approximate_point_size_bytes;
  ret.id = {id, 0};
  ret.name.name = item.name.get();
  ret.name.name_length = item.name_length;
  return ret;
}

std::optional<morton::morton192_t> input_data_source_registry_t::get_done_morton()
{
  std::unique_lock<std::mutex> lock(_mutex);
  for (auto it = _sorted_input_sources.rbegin(); it != _sorted_input_sources.rend(); ++it)
  {
    auto &item = _registry[*it];
    if (item.read_finished && item.inserted_into_tree == item.sub_count)
    {
      return item.morton_min;
    }
  }
  return {};
}

input_data_source_t input_data_source_registry_t::get(input_data_id_t input_id)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto &item = get_item(input_id, _registry);

  input_data_source_t ret;
  ret.input_id = item.input_id;
  ret.attribute_id = item.attribute_id;
  ret.name = input_name_ref_t(item.name.get(), item.name_length);
  ret.public_header = item.public_header;
  return ret;
}

} // namespace points::converter
