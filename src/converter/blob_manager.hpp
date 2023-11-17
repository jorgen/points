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
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <fmt/core.h>  // Include the fmt library header
#include <ankerl/unordered_dense.h>

class blob_manager_t
{
public:
  static constexpr uint64_t PAGE_SIZE = 100 * 1024 * 1024; // 100 MB
  using page_t = uint64_t;

  struct offset_t
  {
    uint64_t data;
    page_t page() const
    {
      return data / PAGE_SIZE;
    }
    bool operator<(const offset_t &o) const
    {
      return data < o.data;
    }
  };

  struct size_type_t
  {
    uint64_t data;
    bool operator<(const size_type_t &s) const
    {
      return data < s.data;
    }
  };

  struct section_t
  {
    offset_t offset;
    size_type_t size;
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
  // Use ankerl::unordered_dense::map for the free sections container
  ankerl::unordered_dense::map<page_t, std::vector<section_t>> _free_sections_by_page;

public:
  blob_manager_t()
    : _next_offset{0}
  {
  }

  [[nodiscard]] offset_t register_blob(size_type_t size)
  {
    for (auto &[page_number, sections] : _free_sections_by_page)
    {
      // Find a section that is big enough to hold the size requested
      auto it = std::find_if(sections.begin(), sections.end(), [&size](const section_t &section) { return section.size.data >= size.data; });

      if (it != sections.end())
      {
        // Found a section, adjust its size or remove it if it matches exactly
        offset_t offset = it->offset;

        if (it->size.data > size.data)
        {
          it->offset.data += size.data;
          it->size.data -= size.data;
        }
        else
        {
          // Erase by swapping with the last element (to avoid shifting elements)
          std::swap(*it, sections.back());
          sections.pop_back();
        }

        // If no sections left, remove the page
        if (sections.empty())
        {
          _free_sections_by_page.erase(page_number);
        }

        return offset;
      }
    }

    // No free section found in existing pages, allocate a new section at the end
    offset_t new_offset = _next_offset;
    _next_offset.data += size.data;
    return new_offset;
  }

  [[nodiscard]] bool unregister_blob(offset_t offset, size_type_t size)
  {
    // Calculate the start and end page
    page_t start_page = offset.page();
    page_t end_page = (offset.data + size.data - 1) / PAGE_SIZE;

    if (offset.data + size.data > _next_offset.data)
    {
      return false; // Trying to unregister a segment that goes beyond the allocated space
    }

    // Iterate over pages that the blob spans
    for (page_t page = start_page; page <= end_page; ++page)
    {
      auto page_it = _free_sections_by_page.find(page);
      if (page_it == _free_sections_by_page.end())
      {
        // If the page doesn't exist, create it
        page_it = _free_sections_by_page.emplace(page, std::vector<section_t>()).first;
      }

      auto &sections = page_it->second;
      // Find the position where the new free section should be inserted
      auto it = std::lower_bound(sections.begin(), sections.end(), offset, [](const section_t &section, const offset_t &val) { return section.offset.data < val.data; });

      if (it != sections.begin() && (it - 1)->offset.data + (it - 1)->size.data > offset.data)
      {
        return false; // Overlaps with the previous free section
      }

      // Check for overlap with the next section, if it exists
      if (it != sections.end() && it->offset.data < offset.data + size.data)
      {
        return false; // Overlaps with the next free section
      }

      // If it's not at the beginning, try to merge with the previous section if adjacent
      if (it != sections.begin() && (it - 1)->offset.data + (it - 1)->size.data == offset.data)
      {
        --it;                       // Move iterator to the previous section to merge
        it->size.data += size.data; // Increase the size of the free section
        offset = it->offset;        // Update the offset to the beginning of the merged section
        // Try to merge with the next section if it is adjacent
        auto next_it = it + 1;
        if (next_it != sections.end() && offset.data + size.data == next_it->offset.data)
        {
          it->size.data += next_it->size.data; // Increase the size of the free section
          sections.erase(next_it);             // Erase the next section as it is now merged
        }
      }
      else
      {
        // If it's at the end or sections is empty, simply append the new free section
        sections.emplace_back(section_t{offset, size});
        it = sections.end() - 1; // Update the iterator to point to the new element
      }

      // Remove the page if no sections left
      if (sections.empty())
      {
        _free_sections_by_page.erase(page_it);
      }
    }

    // Adjust _next_offset if the blob was at the end
    if (offset.data + size.data == _next_offset.data)
    {
      _next_offset.data -= size.data;
    }

    return true;
  }

  size_t get_free_sections_count()
  {
    return std::accumulate(_free_sections_by_page.begin(), _free_sections_by_page.end(), size_t(0), [](size_t sum, const auto &page) { return sum + page.second.size(); });
  }
  section_t get_free_section(page_t page, size_t n)
  {
    return _free_sections_by_page[page][n];
  }

  size_type_t get_file_size()
  {
    return {_next_offset.data};
  }
};

