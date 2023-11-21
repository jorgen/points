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
#include "blob_manager.hpp"

blob_manager_t::blob_manager_t()
: _next_offset{0}
{
}

blob_manager_t::offset_t blob_manager_t::register_blob(size_type_t size)
{
  size_type_t spillover_from_last_page = {0};
  page_t spillover_page = 0;
  for (auto page_it = _free_sections_by_page.begin(); page_it != _free_sections_by_page.end(); ++page_it)
  {
    auto &[page_number, sections] = *page_it;
    auto page_offset = page_number * PAGE_SIZE;
    if (spillover_page + 1 != page_number
      || (sections.size() && sections.front().offset.data != page_offset))
    {
      spillover_from_last_page = {0};
    }
    for (size_t section_index = 0; section_index < sections.size(); section_index++)
    {
      auto &section = sections[section_index];
      auto current_section_size = size_type_t{section.size.data + spillover_from_last_page.data};
      if (current_section_size.data >= size.data)
      {
        offset_t return_offset = {page_offset - spillover_from_last_page.data};

        if (section.size.data == size.data - spillover_from_last_page.data)
        {
          sections.erase(sections.begin() + section_index);
        }
        else
        {
          section.offset.data += size.data - spillover_from_last_page.data;
          section.size.data -= size.data - spillover_from_last_page.data;
        }

        if (sections.empty())
        {
          _free_sections_by_page.erase(page_number);
        }

        if (spillover_from_last_page.data > 0)
        {
          auto spillover_page_count = size.data / PAGE_SIZE;
          auto remove_back_free_section_it = page_it - 1;
          for (uint64_t spillover_page_index = 0; spillover_page_index < spillover_page_count; spillover_page_index++)
          {
            if (remove_back_free_section_it->second.size() == 1)
            {
              remove_back_free_section_it = _free_sections_by_page.erase(remove_back_free_section_it);
            }
            else
            {
              remove_back_free_section_it->second.pop_back();
            }
            if (remove_back_free_section_it != _free_sections_by_page.begin())
            {
              --remove_back_free_section_it;
            }
          }
        }
        return return_offset;
      }
    }

    if (sections.size() && sections.back().offset.data + sections.back().size.data == page_offset + blob_manager_t::PAGE_SIZE)
    {
      spillover_from_last_page.data += sections.back().size.data;
      spillover_page = page_number;
    }
    else
    {
      spillover_from_last_page = {0};
    }
  }

  offset_t new_offset = _next_offset;
  _next_offset.data += size.data;
  return new_offset;
}


[[nodiscard]] bool blob_manager_t::unregister_blob(offset_t total_offset, size_type_t total_size)
{
  page_t start_page = total_offset.page();
  page_t end_page = (total_offset.data + total_size.data - 1) / PAGE_SIZE;

  if (total_offset.data + total_size.data > _next_offset.data)
  {
    return false;
  }

  for (page_t page = start_page; page <= end_page; ++page)
  {
    auto page_it = _free_sections_by_page.find(page);
    if (page_it == _free_sections_by_page.end())
    {
      page_it = _free_sections_by_page.emplace(page, std::vector<section_t>()).first;
    }
    size_type_t page_start = {page * PAGE_SIZE};
    size_type_t page_end = {page_start.data + PAGE_SIZE};
    size_type_t section_end = {std::min(total_offset.data + total_size.data, page_end.data)};
    offset_t offset_for_page = {std::max(total_offset.data, page_start.data)};
    size_type_t size_for_page = {section_end.data - std::max(page_start.data, total_offset.data)};

    auto &sections = page_it->second;
    auto it = std::lower_bound(sections.begin(), sections.end(), offset_for_page, [](const section_t &section, const offset_t &val) { return section.offset.data < val.data; });

    if (it != sections.begin() && (it - 1)->offset.data + (it - 1)->size.data > offset_for_page.data)
    {
      return false;
    }

    if (it != sections.end() && it->offset.data < offset_for_page.data + size_for_page.data)
    {
      return false;
    }

    if (it != sections.begin() && (it - 1)->offset.data + (it - 1)->size.data == offset_for_page.data)
    {
      --it;
      it->size.data += size_for_page.data;
      auto next_it = it + 1;
      if (next_it != sections.end() && it->offset.data + it->size.data == next_it->offset.data)
      {
        it->size.data += next_it->size.data;
        sections.erase(next_it);
      }
    }
    else
    {
      sections.emplace_back(section_t{offset_for_page, size_for_page});
    }

    if (sections.empty())
    {
      _free_sections_by_page.erase(page_it);
    }
  }

  if (total_offset.data + total_size.data == _next_offset.data)
  {
    while (!_free_sections_by_page.empty())
    {
      auto last_page_it = --_free_sections_by_page.end();
      auto &last_section = last_page_it->second.back();
      if (last_section.offset.data + last_section.size.data != _next_offset.data)
      {
        break;
      }

      _next_offset.data -= last_section.size.data;
      if (last_page_it->second.size() == 1)
      {
        _free_sections_by_page.erase(last_page_it);
      }
      else
      {
        last_page_it->second.pop_back();
        break;
      }
    }
  }

  return true;
}

  
size_t blob_manager_t::get_free_sections_count() const
{
  size_t count = 0;
  bool last_page_ended_with_free_section = false;
  for (auto& [page_number, sections] : _free_sections_by_page)
  {
    if (last_page_ended_with_free_section
      && sections.size() && sections.front().offset.data == page_number * PAGE_SIZE)
    {
      count += sections.size() - 1;
    }
    else
    {
      count += sections.size();
    }
    last_page_ended_with_free_section = sections.size() && sections.back().offset.data + sections.back().size.data == (page_number + 1) * PAGE_SIZE;
  }
  return count;
}

size_t blob_manager_t::get_pages_count() const
{
  return _free_sections_by_page.size();
}

blob_manager_t::section_t blob_manager_t::get_free_section(page_t page, size_t n) const
{
  return _free_sections_by_page.at(page)[n];
}

blob_manager_t::size_type_t blob_manager_t::get_file_size() const
{
  return {_next_offset.data};
}
