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
#include <atomic>
#include <cstdint>
#include <vector>

namespace points
{
namespace render
{
  struct buffer_data
  {
    enum class state : uint8_t
    {
      add,
      modify,
      render,
      to_remove,
      removed
    };
    bool ref = true;
    bool rendered = false;
    state state = state::add;
    const void *data = nullptr;
    int data_size = 0;
    int data_offset = 0;
    void *user_ptr = nullptr;
  };
}
} // namespace points
