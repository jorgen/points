/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
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

// Physical reclamation of dead byte ranges inside the (sparse) cache file. Blob offsets are
// permanent identities that can never be reused by the allocator, so hole-punching is the ONLY way
// to give evicted/spilled blobs' bytes back to the filesystem -- the file's logical size is a
// monotone high-water mark, its allocated size is what the cap controls.
//
//   Linux:   fallocate(FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE)  (any alignment)
//   macOS:   fcntl(F_PUNCHHOLE)                                     (block-aligned interior only)
//   Windows: FSCTL_SET_SPARSE (once) + FSCTL_SET_ZERO_DATA
//
// The punch is best-effort by contract: punching only the block-aligned interior of a range keeps
// <1% of the bytes for ~1MB blobs on 4K blocks. Callers must treat `unsupported` as a soft
// degrade (eviction becomes mark-only; the cap is then enforced by spilling instead).

#include "error.hpp"

#include <cstdint>

namespace points::converter
{

enum class hole_punch_status_t
{
  ok,          // range (or its aligned interior) returned to the filesystem
  unsupported, // filesystem/OS cannot punch -- degrade to mark-only eviction
  error,       // I/O error (see errno-style message)
};

struct hole_punch_result_t
{
  hole_punch_status_t status;
  points_error_t error;
};

// Punch `[offset, offset + length)` (its block-aligned interior where the platform requires it) in
// the file behind CRT descriptor `fd`. Zero-length (or a range whose aligned interior is empty) is
// a successful no-op. On Windows the file is flagged sparse on first use.
hole_punch_result_t file_hole_punch(int fd, uint64_t offset, uint64_t length);

// Cheap capability probe for `fd`'s filesystem: punches a zero-impact aligned range at the current
// end of a scratch region the CALLER guarantees is dead (typically done once at open with a probe
// range the backend allocates). Returns ok/unsupported/error like the punch itself.
inline hole_punch_result_t file_hole_punch_probe(int fd, uint64_t offset, uint64_t length)
{
  return file_hole_punch(fd, offset, length);
}

} // namespace points::converter
