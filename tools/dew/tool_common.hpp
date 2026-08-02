/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
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
#pragma once

// Helpers shared by the `dew` CLI subcommands (formatting, RAII handles, stat tables). These used
// to be duplicated static copies in each standalone tool; the subcommands now share this one unit.

#include <dew/converter/converter.h>

#include <argh.h>

#include <cstdint>
#include <initializer_list>
#include <string>

namespace tool
{

const char *type_name(dew_type_t type);
const char *method_name(uint32_t method);
std::string format_bytes(uint64_t bytes);
std::string format_number(uint64_t n);
std::string get_error_string(const dew_error_t *error);

// Strict full-string numeric parses (std::from_chars; no partial consumption, no silent zeroes).
bool parse_u32(const std::string &text, uint32_t &out);
bool parse_u64(const std::string &text, uint64_t &out);

// Validate every option the user passed against a subcommand's known sets. argh routes '--name=value'
// tokens into params() unconditionally and a trailing value-less param into flags(), so checking
// flags() alone misses whole classes of misspellings ('--summary=1', '--frce=true') -- this checks
// both maps and distinguishes "unknown option", "option does not take a value" and "missing value".
// Names are dash-trimmed as argh stores them ("n", "summary"); -h/--help are always allowed.
// Returns false (after printing the error) on the first violation.
bool check_options(const argh::parser &cmdl, std::initializer_list<const char *> flag_names, std::initializer_list<const char *> param_names);

struct converter_handle_t
{
  dew_converter_t *ptr = nullptr;
  explicit converter_handle_t(dew_converter_t *p)
    : ptr(p)
  {
  }
  ~converter_handle_t()
  {
    if (ptr)
      dew_converter_destroy(ptr);
  }
  converter_handle_t(const converter_handle_t &) = delete;
  converter_handle_t &operator=(const converter_handle_t &) = delete;
  operator dew_converter_t *() const
  {
    return ptr;
  }
  explicit operator bool() const
  {
    return ptr != nullptr;
  }
};

struct table_row_t
{
  uint64_t buffer_count;
  uint64_t uncompressed_bytes;
  uint64_t compressed_bytes;
};

void print_attribute_table(const char *title, const dew_converter_stats_t &stats, table_row_t (*get_row)(const dew_converter_attribute_stats_t &));

} // namespace tool
