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
#include "attributes_configs.hpp"

namespace points
{
namespace converter
{

attributes_configs_t::attributes_configs_t()
{

}

static bool compare_attribute(const attribute_t &a, const attribute_t &b)
{
  if (a.name_size != b.name_size)
    return false;
  if (a.format != b.format)
    return false;
  if (a.group != b.group)
    return false;
  if (a.components != b.components)
    return false;
  return memcmp(a.name, b.name, a.name_size) == 0;
}
static bool compare_attributes(const attributes_t &a, const attributes_t &b)
{
  if (a.attributes.size() != b.attributes.size())
    return false;
  for (int i = 1; i < int(a.attributes.size()); i++)
  {
    if (!compare_attribute(a.attributes[i], b.attributes[i]))
      return false;
  }
  return true;
}

attributes_id_t attributes_configs_t::get_attribute_config_index(attributes_t &&attr)
{
  std::unique_lock<std::mutex> lock(_mutex);
  for (int i = 0; i < int(_attributes_configs.size()); i++)
  {
    auto &source = _attributes_configs[i];
    if (compare_attributes(*source, attr))
      return {i};
  }

  int ret = int(_attributes_configs.size());
  _attributes_configs.emplace_back(new attributes_t());
  *_attributes_configs.back() = std::move(attr);
  return {ret};
}

const attributes_t &attributes_configs_t::get(attributes_id_t id)
{
  std::unique_lock<std::mutex> lock(_mutex);
  return *_attributes_configs[id.data].get();
}

}}
