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
#pragma once
#include <ankerl/unordered_dense.h>
#include <conversion_types.hpp>
#include <vector>
namespace points
{
namespace converter
{

class input_storage_map_t
{
public:
  void add_storage(input_data_id_t id, attributes_id_t attributes_id, std::vector<storage_location_t> &&storage);
  std::pair<attributes_id_t, std::vector<storage_location_t>> dereference(input_data_id_t id);
  std::pair<attributes_id_t, std::vector<storage_location_t>> info(input_data_id_t id) const;
  attributes_id_t attribute_id(input_data_id_t id);
  [[nodiscard]] storage_location_t location(input_data_id_t id, int attribute_index) const;
  void add_ref(input_data_id_t id);

private:
  struct hash
  {
    using is_avalanching = void;
    auto operator()(input_data_id_t id) const noexcept -> uint64_t
    {
      uint64_t data;
      static_assert(sizeof(data) == sizeof(id), "size mismatch");
      memcpy(&data, &id, sizeof(data));
      return ankerl::unordered_dense::detail::wyhash::hash(data);
    }
  };

  struct value_t
  {
    attributes_id_t attributes_id;
    std::vector<storage_location_t> storage;
    uint32_t ref_count;
  };
  ankerl::unordered_dense::map<input_data_id_t, value_t, hash> _map;
};

class deref_on_destruct_t
{
public:
  explicit deref_on_destruct_t(input_storage_map_t &map)
    : _map(map)
  {
    _ids.reserve(16);
  }

  ~deref_on_destruct_t()
  {
    for (auto id : _ids)
    {
      _map.dereference(id);
    }
  }

  void add(input_data_id_t id)
  {
    _ids.push_back(id);
  }

  deref_on_destruct_t(const deref_on_destruct_t &) = delete;
  deref_on_destruct_t &operator=(const deref_on_destruct_t &) = delete;
  deref_on_destruct_t(deref_on_destruct_t &&other) noexcept = delete;
  deref_on_destruct_t &operator=(deref_on_destruct_t &&other) noexcept = delete;

private:
  input_storage_map_t &_map;
  std::vector<input_data_id_t> _ids;
};

} // namespace converter
} // namespace points
