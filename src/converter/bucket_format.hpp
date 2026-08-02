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

// The DEW2 bucket layout: how a converted dataset lives in an object store as one immutable object
// per blob plus an append-only band index. Written incrementally by the upload handler as subtrees
// finalize; readable mid-conversion as a consistent morton-prefix.
//
//   manifest              root manifest -- the ONLY mutable object; tiny; atomic-replace commit point
//   bands/{band:08}       immutable band manifest, one per committed band
//   data/{id:08x}         immutable data objects: EXACTLY ONE compressed blob each (one attribute
//                         buffer of one node, a serialized tree, or the terminal registry/attrs) --
//                         object size == blob size, read with a WHOLE-object GET (no Range header)
//   spill/...             cache-tier spill segments + journal (disjoint namespace, see spill_store;
//                         the only place ranged GETs remain -- temporary data)
//
// Addressing: storage_location_t in bucket-remapped trees/manifests is {file_id = object id,
// offset = 0, size = blob length} -> object "data/{file_id:08x}". Readers never need a HEAD: the
// storage map already carries the object's exact size. A COMPLETE dataset reads root manifest ->
// tree registry -> trees -> blobs; band manifests only matter for resume/GC and mid-conversion
// readers. Whole objects make every blob one clean HTTP-cache/CDN entry.
//
// Consistency: data objects < band manifest < root manifest, bands commit strictly in order, and
// the dataset uuid ties the bucket to the cache generation that produced it (mismatch = refuse).

#include "conversion_types.hpp"
#include "error.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dew::converter
{

// ---- object names -------------------------------------------------------------------------------
std::string bucket_data_object_name(uint32_t object_id);
std::string bucket_band_name(uint32_t band_id);
inline const char *bucket_root_manifest_name()
{
  return "manifest";
}

// ---- root manifest (fixed 256 bytes; the commit point) -------------------------------------------
// New datasets are written with the 'DEW2' magic; 'JLP2' is the same layout under the format's old
// name and is accepted everywhere DEW2 is, so datasets converted before the rename keep opening.
// (dew_copy rewrites a bucket and therefore migrates the magic as a side effect.)
constexpr uint32_t k_root_manifest_magic = 0x32574544u;             // 'DEW2'
constexpr uint32_t k_root_manifest_magic_legacy_jlp2 = 0x32504c4au; // 'JLP2' (pre-rename datasets)
constexpr uint32_t k_root_manifest_size = 256;

inline bool is_root_manifest_magic(uint32_t magic)
{
  return magic == k_root_manifest_magic || magic == k_root_manifest_magic_legacy_jlp2;
}
struct root_manifest_t
{
  uint32_t version = 1;
  uint8_t dataset_uuid[16] = {};
  uint8_t complete = 0;      // 1 once the terminal band + registry landed
  uint32_t band_count = 0;   // bands/{0..band_count-1} exist and are committed
  uint32_t next_object_id = 0; // data/{0..next_object_id-1} allocated by committed bands
  // Zero until complete: the final registry/attributes/stats/perf, each its own data object.
  storage_location_t attribute_configs = {};
  storage_location_t tree_registry = {};
  storage_location_t compression_stats = {};
  storage_location_t perf_stats = {};
};

std::shared_ptr<uint8_t[]> serialize_root_manifest(const root_manifest_t &manifest);
[[nodiscard]] dew_error_t deserialize_root_manifest(const uint8_t *data, uint32_t size, root_manifest_t &out);

// ---- band manifest (immutable, one per band) -----------------------------------------------------
constexpr uint32_t k_band_manifest_magic = 0x32444e42u; // 'BND2'
struct band_tree_entry_t
{
  uint32_t tree_id;
  storage_location_t location; // the tree blob's data object (remapped storage map)
};
struct band_dedup_entry_t
{
  uint64_t cache_offset;       // blob identity in the cache file (file_id always 0)
  storage_location_t location; // its bucket object: {object id, 0, size}
};
struct band_manifest_t
{
  uint32_t band_id = 0;
  uint8_t dataset_uuid[16] = {};
  morton::morton192_t watermark = {};
  uint32_t first_object_id = 0;
  uint32_t next_object_id = 0;
  std::vector<band_tree_entry_t> trees;
  std::vector<band_dedup_entry_t> blobs; // every blob FIRST uploaded in this band
  std::vector<uint8_t> attributes_configs_snapshot;
};

std::vector<uint8_t> serialize_band_manifest(const band_manifest_t &manifest);
[[nodiscard]] dew_error_t deserialize_band_manifest(const uint8_t *data, uint32_t size, band_manifest_t &out);

} // namespace dew::converter
