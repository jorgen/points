/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2023  Jørgen Lind
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

#include <algorithm>
#include <deque>
#include <iostream>
#include <optional>
#include <utility>

template <typename key_t, typename value_t>
class deque_map_t
{
public:
  explicit deque_map_t()
  {
  }

  template <typename... args_t>
  [[nodiscard]]
  value_t emplace_back(key_t key, args_t &&...args)
  {
    assert(!contains_key(key));
    _keys.push_back(key);
    _values.emplace_back(std::forward<args_t>(args)...);
    return _values.back();
  }

  value_t &value(const key_t &key)
  {
    auto it = std::find(_keys.begin(), _keys.end(), key);
    auto index = std::distance(_keys.begin(), it);
    return _values.at(index);
  }

  [[nodiscard]]
  bool contains_key(const key_t &key)
  {
    return std::find(_keys.begin(), _keys.end(), key) != _keys.end();
  }

  [[nodiscard]]
  size_t size() const
  {
    return _keys.size();
  }

  bool remove(const key_t &key)
  {
    auto it = std::find(_keys.begin(), _keys.end(), key);
    if (it != _keys.end())
    {
      auto index = std::distance(_keys.begin(), it);
      _keys.erase(it);
      _values.erase(_values.begin() + index);
      return true;
    }
    return false;
  }
private:
  std::deque<key_t> _keys;
  std::deque<value_t> _values;
};
