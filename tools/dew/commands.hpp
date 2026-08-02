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

// The `dew` CLI subcommand entry points. Each receives the argv slice starting at the subcommand
// name (argv[0] == subcommand), parses its own flags with argh, and returns the process exit code.

int cmd_convert(int argc, char **argv);
int cmd_copy(int argc, char **argv);
int cmd_extract(int argc, char **argv);
int cmd_info(int argc, char **argv);
int cmd_laz(int argc, char **argv);
