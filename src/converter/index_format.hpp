/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

#include "conversion_types.hpp"
#include "error.hpp"

#include <cstdint>
#include <cstring>
#include <memory>

namespace points::converter
{

// The dataset index / superblock. A fixed 128-byte block:
//   magic {'J','L','P',0} followed by 5 storage_location_t (16 bytes each) = 84 bytes,
// then the extras region in the previously-zeroed spare bytes (fully backward compatible --
// legacy files carry zeros there, and zero means "absent" for every extra):
//   offset  84: residency_table storage_location_t (16B; zero = no cache-tier residency table)
//   offset 100: dataset_uuid (16B; zero = no destination bound -- ties a cache to its bucket)
//   offset 116: version/flags u32 (zero = legacy semantics)
//   offset 120: 8 spare bytes.
// In packed mode it lives at offset 0 of the single file; in object mode it is the
// content of the "manifest" object. Both backends use these two functions verbatim so
// the on-disk index layout is identical across storage modes.
constexpr uint32_t k_serialized_index_size = 128;

struct index_extras_t
{
  storage_location_t residency_table = {}; // zero = absent
  uint8_t dataset_uuid[16] = {};           // zero = none
  uint32_t version_flags = 0;              // reserved
};
static_assert(4 + 6 * sizeof(storage_location_t) + 16 + 4 <= k_serialized_index_size, "index extras must fit the 128-byte superblock");

inline std::shared_ptr<uint8_t[]> serialize_index(const uint32_t index_size, const storage_location_t &free_blobs, const storage_location_t &attribute_configs, const storage_location_t &tree_registry,
                                                  const storage_location_t &compression_stats, const storage_location_t &perf_stats, const index_extras_t *extras = nullptr)
{
  auto serialized_index = std::make_shared<uint8_t[]>(index_size);
  auto *data = serialized_index.get();
  memset(data, 0, index_size);

  uint8_t magic[] = {'J', 'L', 'P', 0};
  memcpy(data, magic, sizeof(magic));
  data += sizeof(magic);

  memcpy(data, &free_blobs, sizeof(free_blobs));
  data += sizeof(free_blobs);

  memcpy(data, &attribute_configs, sizeof(attribute_configs));
  data += sizeof(attribute_configs);

  memcpy(data, &tree_registry, sizeof(tree_registry));
  data += sizeof(tree_registry);

  memcpy(data, &compression_stats, sizeof(compression_stats));
  data += sizeof(compression_stats);

  memcpy(data, &perf_stats, sizeof(perf_stats));
  data += sizeof(perf_stats);

  if (extras)
  {
    memcpy(data, &extras->residency_table, sizeof(extras->residency_table));
    data += sizeof(extras->residency_table);
    memcpy(data, extras->dataset_uuid, sizeof(extras->dataset_uuid));
    data += sizeof(extras->dataset_uuid);
    memcpy(data, &extras->version_flags, sizeof(extras->version_flags));
  }

  return serialized_index;
}

[[nodiscard]] inline points_error_t deserialize_index(const uint8_t *buffer, uint32_t buffer_size, storage_location_t &free_blobs, storage_location_t &attribute_configs, storage_location_t &tree_registry,
                                                     storage_location_t &compression_stats, storage_location_t &perf_stats, index_extras_t *extras = nullptr)
{
  (void)buffer_size; // buffer size is validated by the caller
  uint8_t magic[] = {'J', 'L', 'P', 0};
  if (memcmp(buffer, magic, sizeof(magic)) != 0)
  {
    points_error_t ret;
    ret.code = 1;
    ret.msg = "Wrong magic.";
    return ret;
  }
  auto ptr = buffer + sizeof(magic);
  memcpy(&free_blobs, ptr, sizeof(free_blobs));
  ptr += sizeof(free_blobs);

  memcpy(&attribute_configs, ptr, sizeof(attribute_configs));
  ptr += sizeof(attribute_configs);

  memcpy(&tree_registry, ptr, sizeof(tree_registry));
  ptr += sizeof(tree_registry);

  memcpy(&compression_stats, ptr, sizeof(compression_stats));
  ptr += sizeof(compression_stats);

  memcpy(&perf_stats, ptr, sizeof(perf_stats));
  ptr += sizeof(perf_stats);

  if (extras)
  {
    // Legacy files carry zeros here (the writer memsets the whole block), which decodes as
    // "no residency table / no uuid / version 0" -- exactly the absent semantics.
    memcpy(&extras->residency_table, ptr, sizeof(extras->residency_table));
    ptr += sizeof(extras->residency_table);
    memcpy(extras->dataset_uuid, ptr, sizeof(extras->dataset_uuid));
    ptr += sizeof(extras->dataset_uuid);
    memcpy(&extras->version_flags, ptr, sizeof(extras->version_flags));
  }
  return {};
}

} // namespace points::converter
