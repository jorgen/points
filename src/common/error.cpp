/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/

#include "error.hpp"

#include "dew/common/error.h"

struct dew_error_t *dew_error_create()
{
  return new dew_error_t();
}

void dew_error_destroy(dew_error_t *error)
{
  delete error;
}
void dew_error_set_info(dew_error_t *error, int code, const char *str, size_t str_len)
{
  error->code = code;
  error->msg = std::string(str, str_len);
}
void dew_error_get_info(const dew_error_t *error, int *code, const char **str, size_t *str_len)
{
  *code = error->code;
  *str = error->msg.c_str();
  *str_len = error->msg.size();
}
