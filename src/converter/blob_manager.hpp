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
#include <deque>
#include <algorithm>
#include <fmt/core.h>  // Include the fmt library header

class blob_manager_t
{
public:
  struct offset_t
  {
    uint64_t data;
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
  };

private:
  offset_t _next_offset;
  std::deque<section_t> _free_sections;
  bool _needs_merging;

public:
  blob_manager_t()
    : _next_offset(0)
    , _needs_merging(false)
  {
  }
  
  [[nodiscard]]
  offset_t register_blob(size_type_t size)
  {
    if (_needs_merging)
    {
      merge_free_sections();
      _needs_merging = false;
    }

    for (auto it = _free_sections.begin(); it != _free_sections.end(); ++it)
    {
      if (it->size.data >= size.data)
      {
        offset_t offset = it->offset;
        if (it->size.data > size.data)
        {
          it->offset.data += size.data;
          it->size.data -= size.data;
        }
        else
        {
          _free_sections.erase(it);
        }
        return offset;
      }
    }
    offset_t offset = _next_offset;
    _next_offset.data += size.data;
    return offset;
  }

  [[nodiscard]]
  bool unregister_blob(offset_t offset, size_type_t size)
  {
    if (offset.data >= _next_offset.data || offset.data + size.data > _next_offset.data)
    {
      // The offset is not valid.
      return false;
    }
    // Ensure we don't try to unregister a blob that starts in a free section.
    auto it = std::lower_bound(_free_sections.begin(), _free_sections.end(), section_t{offset, size}, [](const section_t &a, const section_t &b) { return a.offset.data < b.offset.data; });

    // If the iterator is not at the beginning and the previous section encapsulates the section to unregister, return false.
    if (it != _free_sections.begin())
    {
      auto prev_it = std::prev(it);
      auto free_section_end = prev_it->offset.data + prev_it->size.data;
      if (offset.data < free_section_end)
      {
        // The blob to unregister is inside a free section.
        return false;
      }
    }

    if (it != _free_sections.end())
    {
      auto free_section_start = it->offset.data;
      auto free_section_end = it->offset.data + it->size.data;
      auto end_offset = offset.data + size.data;

      // Check if the unregister section overlaps with the beginning of a free section.
      if (offset.data < free_section_end && end_offset > free_section_start)
      {
        return false;
      }
    }

    if (offset.data + size.data == _next_offset.data)
    {
      // The blob to unregister is at the end of the file.
      _next_offset.data -= size.data;
      if (_free_sections.size())
      {
        auto last_free_section = _free_sections.back();
        auto last_free_end = last_free_section.offset.data + last_free_section.size.data;
        if (last_free_end == _next_offset.data)
        {
          _next_offset.data -= last_free_section.size.data;
          _free_sections.pop_back();
        }
      }
      return true;
    }

    // Now, we can safely unregister the blob.
    // Assuming the offset is correct and the blob is not part of a free section, we can insert it into free sections.
    _free_sections.insert(it, section_t{offset, size});
    _needs_merging = true;

    return true;
  }

  //public for testing purposes
  void merge_free_sections()
  {
    auto it = _free_sections.begin();
    while (it != _free_sections.end())
    {
      auto next_it = std::next(it);
      if (next_it != _free_sections.end() && it->offset.data + it->size.data == next_it->offset.data)
      {
        it->size.data += next_it->size.data; // Merge the two sections
        it = _free_sections.erase(next_it) - 1;       // Remove the next section 
      }
      else
      {
        ++it; // Move to the next section
      }
    }
  }

  void print_status() const
  {
    fmt::print("Free Sections:\n");
    for (const auto &section : _free_sections)
    {
      fmt::print("Offset: {}, Size: {}\n", section.offset.data, section.size.data);
    }
    fmt::print("Next Available Offset: {}\n", _next_offset.data);
  }
  size_t get_free_sections_count()
  {
    return _free_sections.size();
  }
  section_t get_free_section(size_t n)
  {
    return _free_sections[n];
  }

  size_type_t get_file_size()
  {
    return {_next_offset.data};
  }
};

