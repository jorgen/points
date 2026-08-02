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
#ifndef DEW_CONVERTER_EXPORT_H
#define DEW_CONVERTER_EXPORT_H

#ifdef DEW_CONVERTER_STATIC_DEFINE
#  define DEW_CONVERTER_EXPORT
#  define DEW_CONVERTER_NO_EXPORT
#elif defined(_MSC_VER)
#  ifndef DEW_CONVERTER_EXPORT
#    ifdef DEW_CONVERTER_EXPORTS
        /* We are building this library */
#      define DEW_CONVERTER_EXPORT __declspec(dllexport)
#    else
        /* We are using this library */
#      define DEW_CONVERTER_EXPORT __declspec(dllimport)
#    endif
#  endif

#  ifndef DEW_CONVERTER_NO_EXPORT
#    define DEW_CONVERTER_NO_EXPORT
#  endif
#else
#  ifndef DEW_CONVERTER_EXPORT
#    ifdef DEW_CONVERTER_EXPORTS
        /* We are building this library */
#      define DEW_CONVERTER_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define DEW_CONVERTER_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef DEW_CONVERTER_NO_EXPORT
#    define DEW_CONVERTER_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#if defined(_MSC_VER)
#  ifndef DEW_CONVERTER_DEPRECATED
#    define DEW_CONVERTER_DEPRECATED __declspec(deprecated)
#  endif
#else
#  ifndef DEW_CONVERTER_DEPRECATED
#    define DEW_CONVERTER_DEPRECATED __attribute__ ((__deprecated__))
#  endif
#endif

#ifndef DEW_CONVERTER_DEPRECATED_EXPORT
#  define DEW_CONVERTER_DEPRECATED_EXPORT DEW_CONVERTER_EXPORT DEW_CONVERTER_DEPRECATED
#endif

#ifndef DEW_CONVERTER_DEPRECATED_NO_EXPORT
#  define DEW_CONVERTER_DEPRECATED_NO_EXPORT DEW_CONVERTER_NO_EXPORT DEW_CONVERTER_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef DEW_CONVERTER_NO_DEPRECATED
#    define DEW_CONVERTER_NO_DEPRECATED
#  endif
#endif

#endif /* DEW_CONVERTER_EXPORT_H */
