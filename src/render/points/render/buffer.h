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
#ifndef POINTS_BUFFER_H
#define POINTS_BUFFER_H

#include <points/render/export.h>

#ifdef __cplusplus
extern "C" {
#endif

struct points_buffer_t;
POINTS_RENDER_EXPORT void points_buffer_set_rendered(struct points_buffer_t *buffer);
POINTS_RENDER_EXPORT void points_buffer_release_data(struct points_buffer_t *buffer);

#ifdef __cplusplus
}
#endif
#endif //POINTS_BUFFER_H
