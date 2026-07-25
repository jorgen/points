/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2025  Jørgen Lind
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
#ifndef POINTS_CONVERTER_CONNECTION_CLI_H
#define POINTS_CONVERTER_CONNECTION_CLI_H

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace points
{
namespace converter
{
namespace cli
{

// Resolve a connection-string command-line argument (--connection / --source-connection /
// --destination-connection) into the actual connection string, so credentials never need to sit in argv
// (which is world-visible via `ps aux`):
//   "@path"        -> the contents of a file (recommended chmod 600); trailing whitespace is stripped
//   "env:NAME"     -> the value of environment variable NAME
//   anything else  -> the literal string (an inline connection string, or "")
// On success returns true and fills `out`; on failure returns false and sets `error`.
inline bool resolve_connection_spec(const std::string &spec, std::string &out, std::string &error)
{
  if (!spec.empty() && spec[0] == '@')
  {
    const std::string path = spec.substr(1);
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
      error = "cannot open connection file: " + path;
      return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '\t'))
      out.pop_back();
    return true;
  }
  if (spec.rfind("env:", 0) == 0)
  {
    const std::string name = spec.substr(4);
    const char *value = std::getenv(name.c_str());
    if (!value)
    {
      error = "environment variable not set: " + name;
      return false;
    }
    out = value;
    return true;
  }
  out = spec;
  return true;
}

} // namespace cli
} // namespace converter
} // namespace points

#endif // POINTS_CONVERTER_CONNECTION_CLI_H
