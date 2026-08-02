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
#include "file_hole_punch.hpp"

#include <cerrno>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#elif defined(_WIN32)
#include <io.h>
#include <windows.h>
#endif

namespace dew::converter
{

// Alignment the platform requires for a punch to take effect. Linux fallocate handles arbitrary
// ranges itself (partial blocks are zeroed); macOS F_PUNCHHOLE and Windows SET_ZERO_DATA only
// reclaim whole filesystem blocks, so we conservatively punch the 4K-aligned interior. (Compiled
// only where used: the Linux branch never calls it and -Werror=unused-function would trip.)
#if defined(__APPLE__) || defined(_WIN32)
static constexpr uint64_t k_punch_align = 4096;

static bool aligned_interior(uint64_t offset, uint64_t length, uint64_t &aligned_offset, uint64_t &aligned_length)
{
  const uint64_t begin = (offset + k_punch_align - 1) & ~(k_punch_align - 1);
  const uint64_t end = (offset + length) & ~(k_punch_align - 1);
  if (end <= begin)
    return false;
  aligned_offset = begin;
  aligned_length = end - begin;
  return true;
}
#endif

#if defined(__linux__)

hole_punch_result_t file_hole_punch(int fd, uint64_t offset, uint64_t length)
{
  if (length == 0)
    return {hole_punch_status_t::ok, {}};
  if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, int64_t(offset), int64_t(length)) == 0)
    return {hole_punch_status_t::ok, {}};
  if (errno == EOPNOTSUPP || errno == ENOSYS || errno == EINVAL)
    return {hole_punch_status_t::unsupported, {1, "hole punch not supported by filesystem"}};
  return {hole_punch_status_t::error, {errno, strerror(errno)}};
}

#elif defined(__APPLE__)

hole_punch_result_t file_hole_punch(int fd, uint64_t offset, uint64_t length)
{
  if (length == 0)
    return {hole_punch_status_t::ok, {}};
  uint64_t aligned_offset = 0;
  uint64_t aligned_length = 0;
  if (!aligned_interior(offset, length, aligned_offset, aligned_length))
    return {hole_punch_status_t::ok, {}}; // interior empty: nothing reclaimable, not an error
  struct fpunchhole punch = {};
  punch.fp_flags = 0;
  punch.reserved = 0;
  punch.fp_offset = off_t(aligned_offset);
  punch.fp_length = off_t(aligned_length);
  if (fcntl(fd, F_PUNCHHOLE, &punch) == 0)
    return {hole_punch_status_t::ok, {}};
  if (errno == ENOTSUP || errno == EOPNOTSUPP || errno == EINVAL)
    return {hole_punch_status_t::unsupported, {1, "hole punch not supported by filesystem"}};
  return {hole_punch_status_t::error, {errno, strerror(errno)}};
}

#elif defined(_WIN32)

hole_punch_result_t file_hole_punch(int fd, uint64_t offset, uint64_t length)
{
  if (length == 0)
    return {hole_punch_status_t::ok, {}};
  HANDLE handle = HANDLE(_get_osfhandle(fd));
  if (handle == INVALID_HANDLE_VALUE)
    return {hole_punch_status_t::error, {1, "invalid file descriptor"}};

  uint64_t aligned_offset = 0;
  uint64_t aligned_length = 0;
  if (!aligned_interior(offset, length, aligned_offset, aligned_length))
    return {hole_punch_status_t::ok, {}};

  DWORD bytes_returned = 0;
  // Flag the file sparse (idempotent) so zeroed ranges deallocate instead of writing zeros.
  if (!DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes_returned, nullptr))
    return {hole_punch_status_t::unsupported, {1, "sparse files not supported by filesystem"}};

  FILE_ZERO_DATA_INFORMATION zero = {};
  zero.FileOffset.QuadPart = LONGLONG(aligned_offset);
  zero.BeyondFinalZero.QuadPart = LONGLONG(aligned_offset + aligned_length);
  if (!DeviceIoControl(handle, FSCTL_SET_ZERO_DATA, &zero, sizeof(zero), nullptr, 0, &bytes_returned, nullptr))
    return {hole_punch_status_t::error, {int(GetLastError()), "FSCTL_SET_ZERO_DATA failed"}};
  return {hole_punch_status_t::ok, {}};
}

#else

hole_punch_result_t file_hole_punch(int fd, uint64_t offset, uint64_t length)
{
  (void)fd;
  (void)offset;
  (void)length;
  return {hole_punch_status_t::unsupported, {1, "hole punch not implemented on this platform"}};
}

#endif

} // namespace dew::converter
