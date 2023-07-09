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

#include <vector>

template<typename SOURCE_T, typename TARGET_T, typename Comp, typename Update>
void update_vector(std::vector<SOURCE_T> &source, std::vector<TARGET_T> &old, std::vector<TARGET_T> &result, Comp comp, Update update)
{
  result.clear();
  if (source.empty())
    return;

  auto source_it = source.begin();
  auto old_it = old.begin();
  result.reserve(source.size());
  while (source_it != source.end())
  {
    if (old_it == old.end() || comp(*old_it, *source_it) > 0)
    {
      // If there are no more elements in old or the element in old is
      // greater than the element in source, add the element from source
      // to the result vector and advance the source iterator.
      result.emplace_back(std::move(*source_it));
      ++source_it;
    }
    else if (comp(*old_it, *source_it) < 0)
    {
      // If the element in old is less than the element in source,
      // advance the old iterator.
      ++old_it;
    }
    else //if (comp(*old_it, *source_it) == 0)
    {
      // If the elements are equal, update the element in the result
      // vector using the update function and advance both iterators.
      update(old_it, source_it, result);
      ++old_it;
      ++source_it;
    }
  }
}

template<typename SOURCE_T, typename TARGET_T, typename Comp>
void update_vector(std::vector<SOURCE_T> &source, std::vector<TARGET_T> &old, std::vector<TARGET_T> &result, Comp comp)
{
  using source_it = typename std::vector<SOURCE_T>::iterator;
  using target_it = typename std::vector<TARGET_T>::iterator;

  auto update = [](target_it &target, source_it &source, std::vector<TARGET_T> &res) {
    (void)target;
    res.emplace_back(std::move(*source));
  };
  update_vector(source, old, result, comp, update);
}
