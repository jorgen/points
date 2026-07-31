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

// The JLP2 bucket layout: how a converted dataset lives in an object store as immutable packs plus
// an append-only band index. Written incrementally by the upload handler as subtrees finalize;
// readable mid-conversion as a consistent morton-prefix.
//
//   manifest              root manifest -- the ONLY mutable object; tiny; atomic-replace commit point
//   bands/{band:08}       immutable band manifest, one per committed band
//   data/{pack:08x}       immutable packs: whole storage units (points blob + all attribute blobs of
//                         one leaf chunk / LOD node, contiguous, compressed bytes verbatim)
//   spill/...             cache-tier spill segments + journal (disjoint namespace, see spill_store)
//
// Addressing keeps storage_location_t verbatim: file_id = pack id -> object "data/{file_id:08x}",
// offset = byte offset inside the pack, size = blob length. One storage unit is contiguous, so a
// whole node is one ranged GET. A COMPLETE dataset reads exactly like a legacy object dataset:
// root manifest -> tree registry -> trees -> blobs; band manifests only matter for resume/GC and
// mid-conversion readers.
//
// Consistency: pack objects < band manifest < root manifest, bands commit strictly in order, and
// the dataset uuid ties the bucket to the cache generation that produced it (mismatch = refuse).

#include "conversion_types.hpp"
#include "error.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace points::converter
{

// ---- object names -------------------------------------------------------------------------------
std::string bucket_pack_name(uint32_t pack_id);
std::string bucket_band_name(uint32_t band_id);
inline const char *bucket_root_manifest_name()
{
  return "manifest";
}

// ---- pack header (16 bytes at the start of every pack object) -----------------------------------
constexpr uint32_t k_pack_magic = 0x324b504au; // 'JPK2'
constexpr uint32_t k_pack_header_size = 16;
struct pack_header_t
{
  uint32_t magic = k_pack_magic;
  uint32_t version = 1;
  uint32_t band_id = 0;
  uint32_t reserved = 0;
};

// ---- root manifest (fixed 256 bytes; the commit point) -------------------------------------------
constexpr uint32_t k_root_manifest_magic = 0x32504c4au; // 'JLP2'
constexpr uint32_t k_root_manifest_size = 256;
struct root_manifest_t
{
  uint32_t version = 1;
  uint8_t dataset_uuid[16] = {};
  uint8_t complete = 0;    // 1 once the terminal band + registry landed
  uint32_t band_count = 0; // bands/{0..band_count-1} exist and are committed
  uint32_t next_pack_id = 0;
  // Zero until complete: where the final registry/attributes/stats/perf live (inside packs).
  storage_location_t attribute_configs = {};
  storage_location_t tree_registry = {};
  storage_location_t compression_stats = {};
  storage_location_t perf_stats = {};
};

std::shared_ptr<uint8_t[]> serialize_root_manifest(const root_manifest_t &manifest);
[[nodiscard]] points_error_t deserialize_root_manifest(const uint8_t *data, uint32_t size, root_manifest_t &out);

// ---- band manifest (immutable, one per band) -----------------------------------------------------
constexpr uint32_t k_band_manifest_magic = 0x32444e42u; // 'BND2'
struct band_tree_entry_t
{
  uint32_t tree_id;
  storage_location_t location; // the tree blob inside a pack (remapped storage map)
};
struct band_dedup_entry_t
{
  uint64_t cache_offset;       // blob identity in the cache file (file_id always 0)
  storage_location_t location; // where its bytes live in the bucket (pack id / offset / size)
};
struct band_manifest_t
{
  uint32_t band_id = 0;
  uint8_t dataset_uuid[16] = {};
  morton::morton192_t watermark = {};
  uint32_t first_pack_id = 0;
  uint32_t next_pack_id = 0;
  std::vector<band_tree_entry_t> trees;
  std::vector<band_dedup_entry_t> blobs; // every blob FIRST uploaded in this band
  std::vector<uint8_t> attributes_configs_snapshot;
};

std::vector<uint8_t> serialize_band_manifest(const band_manifest_t &manifest);
[[nodiscard]] points_error_t deserialize_band_manifest(const uint8_t *data, uint32_t size, band_manifest_t &out);

} // namespace points::converter
