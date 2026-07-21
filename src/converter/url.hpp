/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

#include <string>

namespace points::converter
{

struct parsed_url_t
{
  std::string scheme; // "" when the input has no "scheme://" prefix (treated as a local file path)
  std::string path;   // everything after "scheme://", or the whole input when there is no scheme
};

// Minimal scheme split. "s3://bucket/key" -> {"s3", "bucket/key"}; "file:///a/b" -> {"file", "/a/b"};
// "/a/b.jlp" -> {"", "/a/b.jlp"}. Scheme is lower-cased; a bare path (no "://") keeps its scheme empty
// so the storage-backend factory can fall back to the single-file (packed) backend.
inline parsed_url_t parse_url(const std::string &url)
{
  parsed_url_t out;
  auto sep = url.find("://");
  if (sep == std::string::npos)
  {
    out.path = url;
    return out;
  }
  out.scheme = url.substr(0, sep);
  for (auto &c : out.scheme)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  out.path = url.substr(sep + 3);
  return out;
}

} // namespace points::converter
