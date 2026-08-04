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
#include "bucket_format.hpp"

#include "memory_writer.hpp"

#include <fmt/format.h>

#include <cassert>
#include <cstring>

namespace dew::converter
{

std::string bucket_data_object_name(uint32_t object_id)
{
  return fmt::format("data/{:08x}", object_id);
}

std::string bucket_band_name(uint32_t band_id)
{
  return fmt::format("bands/{:08}", band_id);
}

std::shared_ptr<uint8_t[]> serialize_root_manifest(const root_manifest_t &manifest)
{
  auto data = std::make_shared<uint8_t[]>(k_root_manifest_size);
  memset(data.get(), 0, k_root_manifest_size);
  uint8_t *ptr = data.get();
  uint8_t *end = ptr + k_root_manifest_size;
  bool ok = write_memory(ptr, end, k_root_manifest_magic);
  ok = ok && write_memory(ptr, end, manifest.version);
  ok = ok && write_memory(ptr, end, manifest.dataset_uuid);
  ok = ok && write_memory(ptr, end, manifest.complete);
  ok = ok && write_memory(ptr, end, manifest.band_count);
  ok = ok && write_memory(ptr, end, manifest.next_object_id);
  ok = ok && write_memory(ptr, end, manifest.attribute_configs);
  ok = ok && write_memory(ptr, end, manifest.tree_registry);
  ok = ok && write_memory(ptr, end, manifest.compression_stats);
  ok = ok && write_memory(ptr, end, manifest.perf_stats);
  assert(ok);
  if (!ok)
    return nullptr;
  return data;
}

dew_error_t deserialize_root_manifest(const uint8_t *data, uint32_t size, root_manifest_t &out)
{
  const dew_error_t invalid = {1, "Invalid DEW2 root manifest"};
  if (size < k_root_manifest_size)
    return invalid;
  const uint8_t *ptr = data;
  const uint8_t *end = data + size;
  uint32_t magic = 0;
  if (!read_memory(ptr, end, magic) || !is_root_manifest_magic(magic))
    return invalid;
  bool ok = read_memory(ptr, end, out.version);
  ok = ok && read_memory(ptr, end, out.dataset_uuid);
  ok = ok && read_memory(ptr, end, out.complete);
  ok = ok && read_memory(ptr, end, out.band_count);
  ok = ok && read_memory(ptr, end, out.next_object_id);
  ok = ok && read_memory(ptr, end, out.attribute_configs);
  ok = ok && read_memory(ptr, end, out.tree_registry);
  ok = ok && read_memory(ptr, end, out.compression_stats);
  ok = ok && read_memory(ptr, end, out.perf_stats);
  if (!ok)
    return invalid;
  return {};
}

std::vector<uint8_t> serialize_band_manifest(const band_manifest_t &manifest)
{
  uint32_t size = 0;
  size += sizeof(k_band_manifest_magic);
  size += sizeof(manifest.band_id);
  size += sizeof(manifest.dataset_uuid);
  size += sizeof(manifest.watermark);
  size += sizeof(manifest.first_object_id);
  size += sizeof(manifest.next_object_id);
  size += sizeof(uint32_t) + uint32_t(manifest.trees.size()) * uint32_t(sizeof(band_tree_entry_t));
  size += sizeof(uint32_t) + uint32_t(manifest.blobs.size()) * uint32_t(sizeof(band_dedup_entry_t));
  size += sizeof(uint32_t) + uint32_t(manifest.attributes_configs_snapshot.size());

  std::vector<uint8_t> out(size);
  uint8_t *ptr = out.data();
  uint8_t *end = ptr + out.size();
  bool ok = write_memory(ptr, end, k_band_manifest_magic);
  ok = ok && write_memory(ptr, end, manifest.band_id);
  ok = ok && write_memory(ptr, end, manifest.dataset_uuid);
  ok = ok && write_memory(ptr, end, manifest.watermark);
  ok = ok && write_memory(ptr, end, manifest.first_object_id);
  ok = ok && write_memory(ptr, end, manifest.next_object_id);
  ok = ok && write_memory(ptr, end, uint32_t(manifest.trees.size()));
  ok = ok && write_vec_type(ptr, end, manifest.trees);
  ok = ok && write_memory(ptr, end, uint32_t(manifest.blobs.size()));
  ok = ok && write_vec_type(ptr, end, manifest.blobs);
  ok = ok && write_memory(ptr, end, uint32_t(manifest.attributes_configs_snapshot.size()));
  if (!manifest.attributes_configs_snapshot.empty())
    ok = ok && write_vec_type(ptr, end, manifest.attributes_configs_snapshot);
  assert(ok && ptr == end);
  if (!ok)
    return {};
  return out;
}

dew_error_t deserialize_band_manifest(const uint8_t *data, uint32_t size, band_manifest_t &out)
{
  const dew_error_t invalid = {1, "Invalid DEW2 band manifest"};
  const uint8_t *ptr = data;
  const uint8_t *end = data + size;
  uint32_t magic = 0;
  if (!read_memory(ptr, end, magic) || magic != k_band_manifest_magic)
    return invalid;
  bool ok = read_memory(ptr, end, out.band_id);
  ok = ok && read_memory(ptr, end, out.dataset_uuid);
  ok = ok && read_memory(ptr, end, out.watermark);
  ok = ok && read_memory(ptr, end, out.first_object_id);
  ok = ok && read_memory(ptr, end, out.next_object_id);
  uint32_t tree_count = 0;
  ok = ok && read_memory(ptr, end, tree_count);
  ok = ok && read_vec_type(ptr, end, out.trees, tree_count);
  uint32_t blob_count = 0;
  ok = ok && read_memory(ptr, end, blob_count);
  ok = ok && read_vec_type(ptr, end, out.blobs, blob_count);
  uint32_t attrs_size = 0;
  ok = ok && read_memory(ptr, end, attrs_size);
  ok = ok && read_vec_type(ptr, end, out.attributes_configs_snapshot, attrs_size);
  if (!ok)
    return invalid;
  return {};
}

} // namespace dew::converter
