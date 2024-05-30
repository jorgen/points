/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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
#include <atomic>
#include <cstdint>
#include <vector>
#include <functional>

#include <points/render/buffer.h>

namespace points
{
namespace render
{

struct buffer_t
{
  bool rendered = false;
  void *user_ptr = nullptr;
  std::function<void()> releaseBuffer;
};
} // namespace render
} // namespace points
