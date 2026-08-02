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
#include "tool_common.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <charconv>

namespace tool
{

bool parse_u32(const std::string &text, uint32_t &out)
{
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  return ec == std::errc{} && ptr == text.data() + text.size() && !text.empty();
}

bool parse_u64(const std::string &text, uint64_t &out)
{
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  return ec == std::errc{} && ptr == text.data() + text.size() && !text.empty();
}

bool check_options(const argh::parser &cmdl, std::initializer_list<const char *> flag_names, std::initializer_list<const char *> param_names)
{
  auto in = [](std::initializer_list<const char *> names, const std::string &name) {
    return std::any_of(names.begin(), names.end(), [&](const char *n) { return name == n; });
  };
  auto dashes = [](const std::string &name) { return name.size() > 1 ? "--" : "-"; };
  for (const auto &flag : cmdl.flags())
  {
    if (flag == "h" || flag == "help" || in(flag_names, flag))
      continue;
    if (in(param_names, flag))
      fmt::print(stderr, "Error: missing value for {}{}\n", dashes(flag), flag);
    else
      fmt::print(stderr, "Error: unknown option '{}{}'\n", dashes(flag), flag);
    return false;
  }
  for (const auto &[param, value] : cmdl.params())
  {
    if (in(param_names, param))
      continue;
    if (in(flag_names, param) || param == "h" || param == "help")
      fmt::print(stderr, "Error: option {}{} does not take a value\n", dashes(param), param);
    else
      fmt::print(stderr, "Error: unknown option '{}{}'\n", dashes(param), param);
    return false;
  }
  return true;
}

const char *type_name(dew_type_t type)
{
  switch (type)
  {
  case dew_type_u8:   return "u8";
  case dew_type_i8:   return "i8";
  case dew_type_u16:  return "u16";
  case dew_type_i16:  return "i16";
  case dew_type_u32:  return "u32";
  case dew_type_i32:  return "i32";
  case dew_type_m32:  return "m32";
  case dew_type_r32:  return "r32";
  case dew_type_u64:  return "u64";
  case dew_type_i64:  return "i64";
  case dew_type_m64:  return "m64";
  case dew_type_r64:  return "r64";
  case dew_type_m128: return "m128";
  case dew_type_m192: return "m192";
  default:            return "?";
  }
}

const char *method_name(uint32_t method)
{
  switch (method)
  {
  case 0:  return "none";
  case 1:  return "blosc2";
  case 2:  return "zstd";
  case 3:  return "huff0";
  default: return "unknown";
  }
}

std::string format_bytes(uint64_t bytes)
{
  if (bytes < 1024)
    return fmt::format("{} B", bytes);
  if (bytes < 1024 * 1024)
    return fmt::format("{:.1f} KB", double(bytes) / 1024.0);
  if (bytes < 1024 * 1024 * 1024)
    return fmt::format("{:.1f} MB", double(bytes) / (1024.0 * 1024.0));
  return fmt::format("{:.2f} GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
}

std::string format_number(uint64_t n)
{
  auto s = std::to_string(n);
  std::string result;
  int count = 0;
  for (int i = int(s.size()) - 1; i >= 0; --i)
  {
    if (count > 0 && count % 3 == 0)
      result.insert(result.begin(), ',');
    result.insert(result.begin(), s[size_t(i)]);
    count++;
  }
  return result;
}

std::string get_error_string(const dew_error_t *error)
{
  int code;
  const char *str;
  size_t str_len;
  dew_error_get_info(error, &code, &str, &str_len);
  return {str, str_len};
}

void print_attribute_table(const char *title, const dew_converter_stats_t &stats, table_row_t (*get_row)(const dew_converter_attribute_stats_t &))
{
  fmt::print("{}:\n", title);
  fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>7s}\n",
             "Attribute", "Format", "Buffers", "Uncompressed", "Compressed", "Ratio");
  fmt::print("{:-<80s}\n", "");

  uint64_t total_uncompressed = 0;
  uint64_t total_compressed = 0;
  uint64_t total_buffers = 0;

  for (uint32_t i = 0; i < stats.attribute_count; i++)
  {
    auto &a = stats.attributes[i];
    auto row = get_row(a);
    if (row.buffer_count == 0)
      continue;
    double ratio = row.compressed_bytes > 0
      ? double(row.uncompressed_bytes) / double(row.compressed_bytes)
      : 0.0;
    fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>6.2f}x\n",
               a.name,
               fmt::format("{}x{}", type_name(a.type), int(a.components)),
               format_number(row.buffer_count),
               format_bytes(row.uncompressed_bytes),
               format_bytes(row.compressed_bytes),
               ratio);
    total_uncompressed += row.uncompressed_bytes;
    total_compressed += row.compressed_bytes;
    total_buffers += row.buffer_count;
  }

  fmt::print("{:-<80s}\n", "");
  double total_ratio = total_compressed > 0
    ? double(total_uncompressed) / double(total_compressed)
    : 0.0;
  fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>6.2f}x\n",
             "Total", "",
             format_number(total_buffers),
             format_bytes(total_uncompressed),
             format_bytes(total_compressed),
             total_ratio);
}

} // namespace tool
