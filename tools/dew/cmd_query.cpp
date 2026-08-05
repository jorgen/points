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

// `dew query` -- the CLI over dew_access.
//
// Distinct from `dew extract`, which introspects the octree and pulls raw attribute BLOBS out of a
// local packed file. This asks the dataset-level question ("what points are in this box"), works
// against any URL the storage layer understands, and decodes to real coordinates.

#include "commands.hpp"

#include <dew/access/query.h>
#include <dew/converter/connection_cli.h>

#include <argh.h>
#include <fmt/printf.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

struct query_args_t
{
  std::string url;
  std::string connection;
  double box_min[3] = {0, 0, 0};
  double box_max[3] = {0, 0, 0};
  bool have_box = false;
  dew_lod_mode_t lod_mode = dew_lod_full;
  int32_t level = 0;
  uint64_t max_points = 0;
  dew_clip_mode_t clip_mode = dew_clip_point;
  dew_position_format_t position_format = dew_position_r64_absolute;
  std::vector<std::string> attributes;
  std::string out_path;
  bool csv = false;
  bool stats_only = false;
};

void print_usage()
{
  fmt::print(stderr, R"(Usage: dew query <dataset> [options]

Query the points inside an axis-aligned box. Unlike `dew extract`, which pulls raw blobs out of a
local packed file, this works against any dataset URL and returns decoded coordinates.

Options:
  --aabb minx,miny,minz,maxx,maxy,maxz   the box (default: the whole dataset)
  --attributes a,b,c                     attributes to fetch alongside the positions
  --lod full|<level>|budget:<n>          how far to descend (default: full resolution)
  --clip node|point                      whole overlapping nodes, or exactly the points
                                         inside the box (default: point)
  --position r64|r32|i32                 coordinate format (default: r64, lossless)
  --connection SPEC                      cloud credentials for s3:// / az:// datasets
  --stats                                print counts only, no point data
  --csv                                  write CSV instead of raw binary
  -o FILE                                write to FILE instead of stdout

Examples:
  dew query scan.dew --stats
  dew query scan.dew --aabb 0,0,0,50,50,20 --attributes intensity --csv -o box.csv
  dew query s3://bucket/scan --aabb 0,0,0,10,10,10 --lod budget:100000 --stats
)");
}

bool parse_triplet_pair(const std::string &text, double out_min[3], double out_max[3])
{
  double values[6];
  int count = 0;
  size_t start = 0;
  while (count < 6 && start <= text.size())
  {
    const size_t comma = text.find(',', start);
    const std::string token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (token.empty())
      return false;
    // strtod, not std::stod: the project builds -fno-exceptions, so a throwing parse is unavailable.
    char *end = nullptr;
    values[count++] = std::strtod(token.c_str(), &end);
    if (end == token.c_str())
      return false;
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  if (count != 6)
    return false;
  for (int i = 0; i < 3; i++)
  {
    out_min[i] = values[i];
    out_max[i] = values[i + 3];
  }
  return true;
}

std::vector<std::string> split_commas(const std::string &text)
{
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size())
  {
    const size_t comma = text.find(',', start);
    auto token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!token.empty())
      out.push_back(std::move(token));
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  return out;
}

const char *type_name(dew_type_t type)
{
  switch (type)
  {
  case dew_type_u8: return "u8";
  case dew_type_i8: return "i8";
  case dew_type_u16: return "u16";
  case dew_type_i16: return "i16";
  case dew_type_u32: return "u32";
  case dew_type_i32: return "i32";
  case dew_type_r32: return "r32";
  case dew_type_u64: return "u64";
  case dew_type_i64: return "i64";
  case dew_type_r64: return "r64";
  default: return "morton";
  }
}

// Print one attribute element as text, for --csv.
void print_element(FILE *out, const dew_attribute_buffer_t &buffer, uint64_t index, uint32_t component)
{
  const auto *bytes = static_cast<const uint8_t *>(buffer.data);
  const uint32_t components = uint32_t(buffer.components);
  const uint64_t slot = index * components + component;
  switch (buffer.type)
  {
  case dew_type_u8: fmt::print(out, "{}", bytes[slot]); break;
  case dew_type_i8: fmt::print(out, "{}", reinterpret_cast<const int8_t *>(bytes)[slot]); break;
  case dew_type_u16: fmt::print(out, "{}", reinterpret_cast<const uint16_t *>(bytes)[slot]); break;
  case dew_type_i16: fmt::print(out, "{}", reinterpret_cast<const int16_t *>(bytes)[slot]); break;
  case dew_type_u32: fmt::print(out, "{}", reinterpret_cast<const uint32_t *>(bytes)[slot]); break;
  case dew_type_i32: fmt::print(out, "{}", reinterpret_cast<const int32_t *>(bytes)[slot]); break;
  case dew_type_r32: fmt::print(out, "{}", reinterpret_cast<const float *>(bytes)[slot]); break;
  case dew_type_u64: fmt::print(out, "{}", reinterpret_cast<const uint64_t *>(bytes)[slot]); break;
  case dew_type_i64: fmt::print(out, "{}", reinterpret_cast<const int64_t *>(bytes)[slot]); break;
  case dew_type_r64: fmt::print(out, "{:.6f}", reinterpret_cast<const double *>(bytes)[slot]); break;
  default: fmt::print(out, "?"); break;
  }
}

} // namespace

int cmd_query(int argc, char **argv)
{
  argh::parser cmdl;
  cmdl.add_params({"--aabb", "--attributes", "--lod", "--clip", "--position", "--connection", "-o", "--output"});
  cmdl.parse(argc, argv);

  if (cmdl[{"-h", "--help"}] || cmdl.size() < 2)
  {
    print_usage();
    return cmdl[{"-h", "--help"}] ? 0 : 1;
  }

  query_args_t args;
  args.url = cmdl[1];
  cmdl({"--connection"}) >> args.connection;
  cmdl({"-o", "--output"}) >> args.out_path;
  args.csv = cmdl["--csv"];
  args.stats_only = cmdl["--stats"];

  std::string box_text;
  if (cmdl({"--aabb"}) >> box_text)
  {
    if (!parse_triplet_pair(box_text, args.box_min, args.box_max))
    {
      fmt::print(stderr, "Error: --aabb needs six comma-separated numbers\n");
      return 1;
    }
    args.have_box = true;
  }

  std::string attribute_text;
  if (cmdl({"--attributes"}) >> attribute_text)
    args.attributes = split_commas(attribute_text);

  std::string lod_text;
  if (cmdl({"--lod"}) >> lod_text && lod_text != "full")
  {
    if (lod_text.rfind("budget:", 0) == 0)
    {
      args.lod_mode = dew_lod_point_budget;
      args.max_points = std::strtoull(lod_text.c_str() + 7, nullptr, 10);
    }
    else
    {
      args.lod_mode = dew_lod_level;
      args.level = int32_t(std::strtol(lod_text.c_str(), nullptr, 10));
    }
  }

  std::string clip_text;
  if (cmdl({"--clip"}) >> clip_text)
    args.clip_mode = (clip_text == "node") ? dew_clip_node : dew_clip_point;

  std::string position_text;
  if (cmdl({"--position"}) >> position_text)
  {
    if (position_text == "r32")
      args.position_format = dew_position_r32_relative;
    else if (position_text == "i32")
      args.position_format = dew_position_i32_grid;
    else if (position_text != "r64")
    {
      fmt::print(stderr, "Error: --position must be r64, r32 or i32\n");
      return 1;
    }
  }

  std::string connection;
  std::string connection_error;
  if (!dew::converter::cli::resolve_connection_spec(args.connection, connection, connection_error))
  {
    fmt::print(stderr, "Connection error: {}\n", connection_error);
    return 1;
  }

  dew_error_t *error = nullptr;
  auto *dataset = dew_dataset_create(args.url.c_str(), uint32_t(args.url.size()), connection.c_str(), uint32_t(connection.size()), nullptr, nullptr, &error);
  // Opening is deferred; wait for it to settle before asking anything of it.
  if (!dataset || dew_dataset_wait_ready(dataset, -1) != dew_dataset_ready)
  {
    if (dataset)
      dew_dataset_get_error(dataset, &error);
    int code = 0;
    const char *message = nullptr;
    size_t length = 0;
    if (error)
      dew_error_get_info(error, &code, &message, &length);
    fmt::print(stderr, "Error: could not open {}: {}\n", args.url, message ? std::string(message, length) : "unknown");
    if (error)
      dew_error_destroy(error);
    if (dataset)
      dew_dataset_close(dataset);
    return 1;
  }

  dew_dataset_info_t info{};
  dew_dataset_get_info(dataset, &info);
  if (!args.have_box)
  {
    // No box given: take the whole dataset. The reported bounds are the root octree cell, which is
    // guaranteed to contain every point.
    for (int i = 0; i < 3; i++)
    {
      args.box_min[i] = info.aabb_min[i];
      args.box_max[i] = info.aabb_max[i];
    }
  }

  std::vector<const char *> attribute_ptrs;
  attribute_ptrs.reserve(args.attributes.size());
  for (const auto &name : args.attributes)
    attribute_ptrs.push_back(name.c_str());

  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = args.box_min[i];
    spec.aabb_max[i] = args.box_max[i];
  }
  spec.lod_mode = args.lod_mode;
  spec.lod = args.level;
  spec.max_points = args.max_points;
  spec.attribute_names = attribute_ptrs.empty() ? nullptr : attribute_ptrs.data();
  spec.attribute_count = uint32_t(attribute_ptrs.size());
  spec.position_format = args.position_format;
  spec.clip_mode = args.clip_mode;

  auto *request = dew_dataset_request_region(dataset, &spec, &error);
  if (!request || dew_request_wait(request, -1) != dew_request_completed)
  {
    if (request)
      dew_request_get_error(request, &error);
    int code = 0;
    const char *message = nullptr;
    size_t length = 0;
    if (error)
      dew_error_get_info(error, &code, &message, &length);
    fmt::print(stderr, "Error: query failed: {}\n", message ? std::string(message, length) : "unknown");
    if (error)
      dew_error_destroy(error);
    if (request)
      dew_request_release(request);
    dew_dataset_close(dataset);
    return 1;
  }

  dew_request_result_t result{};
  if (!dew_request_get_result(request, &result))
  {
    fmt::print(stderr, "Error: no result\n");
    dew_request_release(request);
    dew_dataset_close(dataset);
    return 1;
  }

  fmt::print(stderr, "dataset  {}\n", args.url);
  fmt::print(stderr, "  cell   [{:.3f}, {:.3f}, {:.3f}] .. [{:.3f}, {:.3f}, {:.3f}]  scale {}\n", info.aabb_min[0], info.aabb_min[1], info.aabb_min[2], info.aabb_max[0], info.aabb_max[1], info.aabb_max[2],
             info.scale);
  fmt::print(stderr, "  box    [{:.3f}, {:.3f}, {:.3f}] .. [{:.3f}, {:.3f}, {:.3f}]\n", args.box_min[0], args.box_min[1], args.box_min[2], args.box_max[0], args.box_max[1], args.box_max[2]);
  fmt::print(stderr, "  points {}  nodes {}\n", result.point_count, result.node_count);
  for (uint32_t i = 0; i < result.buffer_count; i++)
  {
    const auto &buffer = result.buffers[i];
    fmt::print(stderr, "  {:<14} {}x{}  {} bytes\n", i == 0 ? "xyz" : std::string(buffer.name, buffer.name_size), type_name(buffer.type), uint32_t(buffer.components), buffer.size_bytes);
  }

  int exit_code = 0;
  if (!args.stats_only && result.point_count > 0)
  {
    FILE *out = stdout;
    if (!args.out_path.empty())
    {
      out = fopen(args.out_path.c_str(), args.csv ? "w" : "wb");
      if (!out)
      {
        fmt::print(stderr, "Error: cannot write {}\n", args.out_path);
        exit_code = 1;
      }
    }
    if (out)
    {
      if (args.csv)
      {
        fmt::print(out, "x,y,z");
        for (uint32_t i = 1; i < result.buffer_count; i++)
        {
          const auto &buffer = result.buffers[i];
          const std::string name(buffer.name, buffer.name_size);
          for (uint32_t c = 0; c < uint32_t(buffer.components); c++)
            fmt::print(out, ",{}{}", name.empty() ? "attr" : name, uint32_t(buffer.components) > 1 ? fmt::format("_{}", c) : std::string());
        }
        fmt::print(out, "\n");
        for (uint64_t p = 0; p < result.point_count; p++)
        {
          for (uint32_t c = 0; c < 3; c++)
          {
            if (c)
              fmt::print(out, ",");
            print_element(out, result.buffers[0], p, c);
          }
          for (uint32_t i = 1; i < result.buffer_count; i++)
          {
            for (uint32_t c = 0; c < uint32_t(result.buffers[i].components); c++)
            {
              fmt::print(out, ",");
              print_element(out, result.buffers[i], p, c);
            }
          }
          fmt::print(out, "\n");
        }
      }
      else
      {
        // Raw: each attribute's buffer back to back, positions first. The stderr summary above says
        // how to interpret them.
        for (uint32_t i = 0; i < result.buffer_count; i++)
          fwrite(result.buffers[i].data, 1, size_t(result.buffers[i].size_bytes), out);
      }
      if (out != stdout)
        fclose(out);
    }
  }

  dew_request_release(request);
  dew_dataset_close(dataset);
  return exit_code;
}
