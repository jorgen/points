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
#include "commands.hpp"
#include "tool_common.hpp"

#include <argh.h>
#include <fmt/format.h>
#include <laszip_api.h>

#include <cstring>
#include <string>

namespace
{

struct laz_args_t
{
  std::string path;
  bool vlrs = false;
  bool points = false;
  bool csv = false;
  uint64_t offset = 0;
  uint64_t count = 10;
};

void print_usage()
{
  fmt::print(stderr, "Usage: dew laz <file.las|file.laz> [options]\n\n");
  fmt::print(stderr, "Introspect a LAS/LAZ file. Without options, prints the header summary.\n\n");
  fmt::print(stderr, "Options:\n");
  fmt::print(stderr, "  --vlrs           also dump each variable-length record\n");
  fmt::print(stderr, "  --points         print points (implied by --offset / -n)\n");
  fmt::print(stderr, "  --offset N       first point to print (default: 0)\n");
  fmt::print(stderr, "  -n, --count M    number of points to print (default: 10)\n");
  fmt::print(stderr, "  --csv            machine-readable point output only (suppresses header + VLR text)\n");
}

// The RAII shape mirrors laszip_handle_t in laszip_file_convert_callbacks.cpp.
struct laz_reader_t
{
  laszip_POINTER reader = nullptr;
  bool open = false;
  ~laz_reader_t()
  {
    if (open)
      laszip_close_reader(reader);
    if (reader)
      laszip_destroy(reader);
  }
};

std::string laz_error(laszip_POINTER reader)
{
  laszip_CHAR *msg = nullptr;
  if (reader && laszip_get_error(reader, &msg) == 0 && msg && *msg)
    return msg;
  return "unknown laszip error";
}

// Fixed-width header text fields are not guaranteed to be null-terminated.
std::string fixed_str(const laszip_CHAR *data, size_t max_len)
{
  size_t len = 0;
  while (len < max_len && data[len] != '\0')
    len++;
  return {data, len};
}

// Which optional per-point fields a point-data-format carries.
bool format_has_gps_time(uint8_t format)
{
  return format == 1 || (format >= 3 && format <= 10);
}

bool format_has_rgb(uint8_t format)
{
  return format == 2 || format == 3 || format == 5 || format == 7 || format == 8 || format == 10;
}

void print_header(const laszip_header_struct &h, bool is_compressed, uint64_t point_count, const std::string &path)
{
  fmt::print("File:            {}{}\n", path, is_compressed ? " (LAZ compressed)" : " (LAS uncompressed)");
  fmt::print("LAS version:     {}.{}\n", h.version_major, h.version_minor);
  fmt::print("Source ID:       {}\n", h.file_source_ID);
  fmt::print("System id:       {}\n", fixed_str(h.system_identifier, 32));
  fmt::print("Software:        {}\n", fixed_str(h.generating_software, 32));
  if (h.file_creation_year != 0)
    fmt::print("Created:         day {} of {}\n", h.file_creation_day, h.file_creation_year);
  fmt::print("Header size:     {} (points at offset {})\n", h.header_size, h.offset_to_point_data);
  fmt::print("Point format:    {} ({} bytes/point)\n", h.point_data_format, h.point_data_record_length);
  fmt::print("Points:          {}{}\n", tool::format_number(point_count), h.number_of_point_records == 0 && point_count > 0 ? " (extended, LAS 1.4)" : "");

  bool any_by_return = false;
  for (int i = 0; i < 15; i++)
  {
    uint64_t n = i < 5 && h.number_of_point_records != 0 ? h.number_of_points_by_return[i] : h.extended_number_of_points_by_return[i];
    if (n == 0)
      continue;
    if (!any_by_return)
      fmt::print("Points/return:  ");
    any_by_return = true;
    fmt::print(" {}:{}", i + 1, tool::format_number(n));
  }
  if (any_by_return)
    fmt::print("\n");

  fmt::print("Scale:           {:g} {:g} {:g}\n", h.x_scale_factor, h.y_scale_factor, h.z_scale_factor);
  fmt::print("Offset:          {:g} {:g} {:g}\n", h.x_offset, h.y_offset, h.z_offset);
  fmt::print("Min:             {:.3f} {:.3f} {:.3f}\n", h.min_x, h.min_y, h.min_z);
  fmt::print("Max:             {:.3f} {:.3f} {:.3f}\n", h.max_x, h.max_y, h.max_z);
  fmt::print("VLRs:            {}\n", h.number_of_variable_length_records);
  if (h.number_of_extended_variable_length_records)
    fmt::print("EVLRs:           {}\n", h.number_of_extended_variable_length_records);
}

void print_vlrs(const laszip_header_struct &h)
{
  if (h.number_of_variable_length_records == 0 || !h.vlrs)
    return;
  fmt::print("\nVariable-length records:\n");
  for (uint32_t i = 0; i < h.number_of_variable_length_records; i++)
  {
    const auto &v = h.vlrs[i];
    fmt::print("  [{}] user_id={:<16} record_id={:<5} length={:<6} {}\n",
               i, fixed_str(v.user_id, 16), v.record_id, v.record_length_after_header, fixed_str(v.description, 32));
  }
}

int print_points(laz_reader_t &laz, const laszip_header_struct &h, uint64_t point_count, const laz_args_t &args)
{
  const bool gps_early = format_has_gps_time(h.point_data_format);
  const bool rgb_early = format_has_rgb(h.point_data_format);
  if (point_count == 0)
  {
    if (args.csv)
      fmt::print("index,x,y,z,intensity,return,returns,classification{}{}\n", gps_early ? ",gps_time" : "", rgb_early ? ",r,g,b" : "");
    else
      fmt::print("File contains no points.\n");
    return 0;
  }
  if (args.offset >= point_count)
  {
    fmt::print(stderr, "Error: --offset {} is past the last point ({} points)\n", args.offset, point_count);
    return 1;
  }
  if (args.offset > 0xFFFFFFFFull)
  {
    // laszip_seek_point truncates its index to 32 bits internally (laszip_dll.cpp seek((U32)...)),
    // which would silently read the wrong points instead of failing.
    fmt::print(stderr, "Error: --offset beyond 4294967295 is not supported by the bundled laszip\n");
    return 1;
  }
  uint64_t count = args.count;
  if (count > point_count - args.offset) // avoids offset+count overflow
    count = point_count - args.offset;

  if (args.offset > 0 && laszip_seek_point(laz.reader, laszip_I64(args.offset)) != 0)
  {
    fmt::print(stderr, "Error: seek to point {} failed: {}\n", args.offset, laz_error(laz.reader));
    return 1;
  }

  laszip_point_struct *point = nullptr;
  if (laszip_get_point_pointer(laz.reader, &point) != 0 || !point)
  {
    fmt::print(stderr, "Error: {}\n", laz_error(laz.reader));
    return 1;
  }

  const bool gps = format_has_gps_time(h.point_data_format);
  const bool rgb = format_has_rgb(h.point_data_format);

  if (args.csv)
  {
    fmt::print("index,x,y,z,intensity,return,returns,classification{}{}\n", gps ? ",gps_time" : "", rgb ? ",r,g,b" : "");
  }
  else
  {
    fmt::print("\n{:>12} {:>14} {:>14} {:>12} {:>9} {:>7} {:>5}{}{}\n",
               "index", "x", "y", "z", "intensity", "ret/cnt", "class", gps ? fmt::format(" {:>18}", "gps_time") : "", rgb ? fmt::format(" {:>17}", "rgb") : "");
  }

  for (uint64_t i = 0; i < count; i++)
  {
    if (laszip_read_point(laz.reader) != 0)
    {
      fmt::print(stderr, "Error: reading point {} failed: {}\n", args.offset + i, laz_error(laz.reader));
      return 1;
    }
    const double x = double(point->X) * h.x_scale_factor + h.x_offset;
    const double y = double(point->Y) * h.y_scale_factor + h.y_offset;
    const double z = double(point->Z) * h.z_scale_factor + h.z_offset;
    // LAS 1.4 point formats (6+) carry the wider extended fields; legacy formats the 3-bit ones.
    const bool extended = h.point_data_format >= 6;
    const unsigned ret = extended ? point->extended_return_number : point->return_number;
    const unsigned num_ret = extended ? point->extended_number_of_returns : point->number_of_returns;
    const unsigned classification = extended ? point->extended_classification : point->classification;

    if (args.csv)
    {
      fmt::print("{},{:.3f},{:.3f},{:.3f},{},{},{},{}", args.offset + i, x, y, z, point->intensity, ret, num_ret, classification);
      if (gps)
        fmt::print(",{:.6f}", point->gps_time);
      if (rgb)
        fmt::print(",{},{},{}", point->rgb[0], point->rgb[1], point->rgb[2]);
      fmt::print("\n");
    }
    else
    {
      fmt::print("{:>12} {:>14.3f} {:>14.3f} {:>12.3f} {:>9} {:>5}/{:<1} {:>5}", args.offset + i, x, y, z, point->intensity, ret, num_ret, classification);
      if (gps)
        fmt::print(" {:>18.6f}", point->gps_time);
      if (rgb)
        fmt::print(" {:>5},{:>5},{:>5}", point->rgb[0], point->rgb[1], point->rgb[2]);
      fmt::print("\n");
    }
  }
  return 0;
}

} // namespace

int cmd_laz(int argc, char **argv)
{
  argh::parser cmdl;
  cmdl.add_params({"--offset", "-n", "--count"});
  cmdl.parse(argc, argv);

  if (cmdl[{"-h", "--help"}] || cmdl.pos_args().size() < 2)
  {
    print_usage();
    return cmdl[{"-h", "--help"}] ? 0 : 1;
  }

  if (!tool::check_options(cmdl, {"vlrs", "points", "csv"}, {"offset", "n", "count"}))
    return 1;

  laz_args_t args;
  args.path = cmdl[1];
  args.vlrs = cmdl["--vlrs"];
  args.csv = cmdl["--csv"];
  const bool has_offset = static_cast<bool>(cmdl("--offset"));
  const bool has_count = static_cast<bool>(cmdl({"-n", "--count"}));
  args.points = cmdl["--points"] || args.csv || has_offset || has_count;
  if (has_offset && !tool::parse_u64(cmdl("--offset").str(), args.offset))
  {
    fmt::print(stderr, "Error: --offset requires a non-negative integer, got '{}'\n", cmdl("--offset").str());
    return 1;
  }
  if (has_count && !tool::parse_u64(cmdl({"-n", "--count"}).str(), args.count))
  {
    fmt::print(stderr, "Error: -n/--count requires a non-negative integer, got '{}'\n", cmdl({"-n", "--count"}).str());
    return 1;
  }

  laz_reader_t laz;
  if (laszip_create(&laz.reader) != 0)
  {
    fmt::print(stderr, "Error: failed to create laszip reader\n");
    return 1;
  }
  laszip_BOOL is_compressed = 0;
  if (laszip_open_reader(laz.reader, args.path.c_str(), &is_compressed) != 0)
  {
    fmt::print(stderr, "Error: failed to open '{}': {}\n", args.path, laz_error(laz.reader));
    return 1;
  }
  laz.open = true;

  laszip_header_struct *header = nullptr;
  if (laszip_get_header_pointer(laz.reader, &header) != 0 || !header)
  {
    fmt::print(stderr, "Error: {}\n", laz_error(laz.reader));
    return 1;
  }

  const uint64_t point_count = header->number_of_point_records != 0 ? header->number_of_point_records : header->extended_number_of_point_records;

  if (!args.csv)
  {
    print_header(*header, is_compressed != 0, point_count, args.path);
    if (args.vlrs)
      print_vlrs(*header);
  }
  if (args.points)
    return print_points(laz, *header, point_count, args);
  return 0;
}
