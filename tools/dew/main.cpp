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

#include <fmt/format.h>

#include <cstring>
#include <string_view>

#ifndef DEW_CLI_VERSION
#define DEW_CLI_VERSION "dev"
#endif

namespace
{

struct command_t
{
  const char *name;
  int (*run)(int argc, char **argv);
  const char *blurb;
};

constexpr command_t k_commands[] = {
  {"convert", cmd_convert, "convert point cloud input (LAS/LAZ) into a .dew dataset, locally or straight into a bucket"},
  {"info", cmd_info, "compression / performance / cache statistics for a dataset"},
  {"extract", cmd_extract, "inspect a dataset's octree and extract attribute buffers"},
  {"copy", cmd_copy, "copy a dataset between storage locations (packed file, dir://, s3://, az://)"},
  {"laz", cmd_laz, "introspect a LAS/LAZ file: header, VLRs, point subranges"},
  {"query", cmd_query, "query the points inside a box and write them out (CSV or raw)"},
};

void print_usage()
{
  fmt::print(stderr, "dewfall {} -- point clouds, settled.\n\n", DEW_CLI_VERSION);
  fmt::print(stderr, "Usage: dew <command> [options]\n\nCommands:\n");
  for (const auto &cmd : k_commands)
    fmt::print(stderr, "  {:<10} {}\n", cmd.name, cmd.blurb);
  fmt::print(stderr, "\nRun 'dew help <command>' (or 'dew <command> --help') for command-specific options.\n");
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    print_usage();
    return 1;
  }

  std::string_view first = argv[1];
  if (first == "-h" || first == "--help")
  {
    print_usage();
    return 0;
  }
  if (first == "--version")
  {
    fmt::print("dew {}\n", DEW_CLI_VERSION);
    return 0;
  }
  if (first == "help")
  {
    if (argc < 3)
    {
      print_usage();
      return 0;
    }
    // `dew help X` == `dew X --help`
    for (const auto &cmd : k_commands)
    {
      if (cmd.name == std::string_view(argv[2]))
      {
        char help_flag[] = "--help";
        char *sub_argv[] = {argv[2], help_flag, nullptr};
        return cmd.run(2, sub_argv);
      }
    }
    fmt::print(stderr, "dew: unknown command '{}'\n\n", argv[2]);
    print_usage();
    return 1;
  }

  for (const auto &cmd : k_commands)
  {
    if (cmd.name == first)
      return cmd.run(argc - 1, argv + 1);
  }

  fmt::print(stderr, "dew: unknown command '{}'\n\n", argv[1]);
  print_usage();
  return 1;
}
