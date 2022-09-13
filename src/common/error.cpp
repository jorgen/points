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

#include "error.hpp"

#include "points/common/error.h"

namespace points
{
error_t *error_create()
{
  return new error_t();
}

void error_destroy(error_t *error)
{
  delete error;
}
void error_set_info(error_t *error, int code, const char *str, size_t str_len)
{
  error->code = code;
  error->msg = std::string(str, str_len);
}
void error_get_info(const error_t *error, int *code, const char **str, size_t *str_len)
{
  *code = error->code;
  *str = error->msg.c_str();
  *str_len = error->msg.size();
}
} // namespace points
