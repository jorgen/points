/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2023  Jørgen Lind
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
#include <ankerl/unordered_dense.h>
#include <fmt/core.h>
#include <vector>

struct serialized_free_blob_manager_t
{
  std::shared_ptr<uint8_t[]> data;
  uint32_t size;
  uint64_t offset;
};

class free_blob_manager_t
{
public:
  static constexpr uint32_t FREE_BLOB_MANAGER_PAGE_SIZE = 100 * 1024 * 1024; // 100 MB
  using page_t = uint32_t;

  struct offset_t
  {
    uint64_t data;
    [[nodiscard]] page_t page() const
    {
      return page_t(data / FREE_BLOB_MANAGER_PAGE_SIZE);
    }
    bool operator<(const offset_t &o) const
    {
      return data < o.data;
    }
  };

  struct blob_size_t
  {
    uint32_t data;
    bool operator<(const blob_size_t &s) const
    {
      return data < s.data;
    }
  };

  struct section_t
  {
    offset_t offset;
    blob_size_t size;
    bool operator<(const section_t &rhs) const
    {
      return offset < rhs.offset;
    }
    page_t page() const
    {
      return offset.page();
    }
  };

private:
  offset_t _next_offset;
  ankerl::unordered_dense::map<page_t, std::vector<section_t>> _free_sections_by_page;

public:
  free_blob_manager_t();

  [[nodiscard]] offset_t register_blob(blob_size_t size);
  [[nodiscard]] bool unregister_blob(offset_t original_offset, blob_size_t size);
  size_t get_free_sections_count() const;
  size_t get_pages_count() const;
  section_t get_free_section(page_t page, size_t n) const;
  offset_t get_file_size() const;
  uint32_t calculate_serialized_size() const;
  serialized_free_blob_manager_t serialize();
};
