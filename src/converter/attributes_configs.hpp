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
#ifndef ATTRIBUTES_CONFIGS_HPP
#define ATTRIBUTES_CONFIGS_HPP

#include <memory>
#include <vector>
#include <mutex>

#include <conversion_types.hpp>

namespace points
{
namespace converter
{

class attributes_configs_t
{
public:
  attributes_configs_t();

  attributes_id_t get_attribute_config_index(attributes_t &&attr);

  const attributes_t &get(attributes_id_t id);
private:
  std::mutex _mutex;
  std::vector<std::unique_ptr<attributes_t>> _attributes_configs;
};

}}
#endif // ATTRIBUTES_CONFIGS_HPP
