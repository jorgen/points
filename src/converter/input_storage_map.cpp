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
//
// Created by jorgen on 26.02.2024.
//

#include "input_storage_map.hpp"
#include "memory_writer.hpp"
namespace points
{
namespace converter
{
void input_storage_map_t::add_storage(input_data_id_t id, attributes_id_t attributes_id, std::vector<storage_location_t> &&storage)
{
  assert(storage.size());
  auto &value = _map[id];
  value.attributes_id = attributes_id;
  value.storage = std::move(storage);
  value.ref_count++;
}
std::pair<attributes_id_t, std::vector<storage_location_t>> input_storage_map_t::dereference(input_data_id_t id)
{
  assert(_map.contains(id));
  auto &value = _map[id];
  value.ref_count--;
  if (value.ref_count == 0)
  {
    auto ret = std::make_pair(value.attributes_id, std::move(value.storage));
    _map.erase(id);
    return ret;
  }
  return std::make_pair(value.attributes_id, value.storage);
}

std::pair<attributes_id_t, std::vector<storage_location_t>> input_storage_map_t::info(input_data_id_t id) const
{
  assert(_map.contains(id));
  auto &value = _map.at(id);
  return std::make_pair(value.attributes_id, value.storage);
}

storage_location_t input_storage_map_t::location(input_data_id_t id, int attribute_index) const
{
  assert(_map.contains(id));
  auto &value = _map.at(id);
  return value.storage[attribute_index];
}
void input_storage_map_t::add_ref(input_data_id_t id)
{
  assert(_map.contains(id));
  auto &value = _map[id];
  value.ref_count++;
}
attributes_id_t input_storage_map_t::attribute_id(input_data_id_t id)
{
  assert(_map.contains(id));
  auto &value = _map[id];
  return value.attributes_id;
}
uint32_t input_storage_map_t::serialized_size() const
{
  uint32_t size = 0;
  size += sizeof(uint32_t); // map size
  for (auto &[id, value] : _map)
  {
    size += sizeof(id);
    size += sizeof(value.attributes_id);
    size += sizeof(value.ref_count);
    size += sizeof(uint32_t);
    size += uint32_t(value.storage.size()) * sizeof(value.storage[0]);
  }
  return size;
}

std::pair<bool, uint8_t *> input_storage_map_t::serialize(uint8_t *buffer, const uint8_t *end) const
{
  uint8_t *ptr = buffer;

  auto map_size = uint32_t(_map.size());
  if (!write_memory(ptr, end, map_size))
    return {false, ptr};

  for (auto &[id, value] : _map)
  {
    if (!write_memory(ptr, end, id))
      return {false, ptr};
    if (!write_memory(ptr, end, value.attributes_id))
      return {false, ptr};
    if (!write_memory(ptr, end, value.ref_count))
      return {false, ptr};
    auto storage_size = uint32_t(value.storage.size());
    if (!write_memory(ptr, end, storage_size))
      return {false, ptr};
    if (!write_vec_type(ptr, end, value.storage))
      return {false, ptr};
  }
  return {true, ptr};
}

std::pair<bool, const uint8_t *> input_storage_map_t::deserialize(const uint8_t *buffer, const uint8_t *end)
{
  auto ptr = buffer;
  uint32_t map_size;
  if (!read_memory(ptr, end, map_size))
    return {false, ptr};
  for (uint32_t i = 0; i < map_size; i++)
  {
    input_data_id_t id;
    attributes_id_t attributes_id;
    uint32_t ref_count;
    uint32_t storage_size;
    std::vector<storage_location_t> storage;
    if (!read_memory(ptr, end, id))
      return {false, ptr};
    if (!read_memory(ptr, end, attributes_id))
      return {false, ptr};
    if (!read_memory(ptr, end, ref_count))
      return {false, ptr};
    if (!read_memory(ptr, end, storage_size))
      return {false, ptr};
    if (!read_vec_type(ptr, end, storage, storage_size))
      return {false, ptr};
    _map[id] = {attributes_id, std::move(storage), ref_count};
  }
  return {true, ptr};
}

} // namespace converter
} // namespace points
