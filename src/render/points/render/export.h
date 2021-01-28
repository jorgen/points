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
#ifndef POINTS_RENDER_EXPORT_H
#define POINTS_RENDER_EXPORT_H

#ifdef POINTS_RENDER_STATIC_DEFINE
#  define POINTS_RENDER_EXPORT
#  define POINTS_RENDER_NO_EXPORT
#elif defined(_MSC_VER)
#  ifndef POINTS_RENDER_EXPORT
#    ifdef points_render_EXPORTS
        /* We are building this library */
#      define POINTS_RENDER_EXPORT __declspec(dllexport)
#    else
        /* We are using this library */
#      define POINTS_RENDER_EXPORT __declspec(dllimport)
#    endif
#  endif

#  ifndef POINTS_RENDER_NO_EXPORT
#    define POINTS_RENDER_NO_EXPORT
#  endif
#else
#  ifndef POINTS_RENDER_EXPORT
#    ifdef points_render_EXPORTS
        /* We are building this library */
#      define POINTS_RENDER_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define POINTS_RENDER_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef POINTS_RENDER_NO_EXPORT
#    define POINTS_RENDER_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#if defined(_MSC_VER)
#  ifndef POINTS_RENDER_DEPRECATED
#    define POINTS_RENDER_DEPRECATED __declspec(deprecated)
#  endif
#else
#  ifndef POINTS_RENDER_DEPRECATED
#    define POINTS_RENDER_DEPRECATED __attribute__ ((__deprecated__))
#  endif
#endif

#ifndef POINTS_RENDER_DEPRECATED_EXPORT
#  define POINTS_RENDER_DEPRECATED_EXPORT POINTS_RENDER_EXPORT POINTS_RENDER_DEPRECATED
#endif

#ifndef POINTS_RENDER_DEPRECATED_NO_EXPORT
#  define POINTS_RENDER_DEPRECATED_NO_EXPORT POINTS_RENDER_NO_EXPORT POINTS_RENDER_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef POINTS_RENDER_NO_DEPRECATED
#    define POINTS_RENDER_NO_DEPRECATED
#  endif
#endif

#endif /* POINTS_RENDER_EXPORT_H */
