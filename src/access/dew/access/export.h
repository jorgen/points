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
#ifndef DEW_ACCESS_EXPORT_H
#define DEW_ACCESS_EXPORT_H

#ifdef DEW_ACCESS_STATIC_DEFINE
#  define DEW_ACCESS_EXPORT
#  define DEW_ACCESS_NO_EXPORT
#elif defined(_MSC_VER)
#  ifndef DEW_ACCESS_EXPORT
#    ifdef DEW_ACCESS_EXPORTS
        /* We are building this library */
#      define DEW_ACCESS_EXPORT __declspec(dllexport)
#    else
        /* We are using this library */
#      define DEW_ACCESS_EXPORT __declspec(dllimport)
#    endif
#  endif

#  ifndef DEW_ACCESS_NO_EXPORT
#    define DEW_ACCESS_NO_EXPORT
#  endif
#else
#  ifndef DEW_ACCESS_EXPORT
#    ifdef DEW_ACCESS_EXPORTS
        /* We are building this library */
#      define DEW_ACCESS_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define DEW_ACCESS_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef DEW_ACCESS_NO_EXPORT
#    define DEW_ACCESS_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#if defined(_MSC_VER)
#  ifndef DEW_ACCESS_DEPRECATED
#    define DEW_ACCESS_DEPRECATED __declspec(deprecated)
#  endif
#else
#  ifndef DEW_ACCESS_DEPRECATED
#    define DEW_ACCESS_DEPRECATED __attribute__ ((__deprecated__))
#  endif
#endif

#ifndef DEW_ACCESS_DEPRECATED_EXPORT
#  define DEW_ACCESS_DEPRECATED_EXPORT DEW_ACCESS_EXPORT DEW_ACCESS_DEPRECATED
#endif

#ifndef DEW_ACCESS_DEPRECATED_NO_EXPORT
#  define DEW_ACCESS_DEPRECATED_NO_EXPORT DEW_ACCESS_NO_EXPORT DEW_ACCESS_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef DEW_ACCESS_NO_DEPRECATED
#    define DEW_ACCESS_NO_DEPRECATED
#  endif
#endif

#endif /* DEW_ACCESS_EXPORT_H */
