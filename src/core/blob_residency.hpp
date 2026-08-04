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

// Residency tracking for the cached-file backend: which data blobs still have local bytes, which
// live (only) in the destination bucket or the spill area, and which local bytes may be physically
// reclaimed (hole-punched).
//
// Key invariant this design rests on: storage_location_t{file_id=0, size, offset} is a blob's
// PERMANENT identity -- tree storage maps reference it forever and are never rewritten -- so the
// offset of an evicted/spilled blob can never be returned to the free_blob_manager. Physical
// reclaim is therefore hole-punching the (sparse) cache file, and `offset` doubles as the stable
// key here. Blobs in the implicit LOCAL state have NO entry at all: until upload/spill pressure
// exists, the map is empty and the read hot path pays one lookup in an empty table.
//
// Threading: single-threaded by design -- every method must be called on the storage loop.
//
// Two-phase durability rule (mirrors the checkpoint `freed` barrier): transitions that only ADD a
// remote copy (mark_uploaded / mark_spilled*) may happen any time; DESTRUCTIVE actions (punching
// local bytes, deleting a spill segment) are allowed only once the remote fact is recorded in a
// committed checkpoint. Entries carry a generation; serialize() snapshots the current generation
// and commit_durable(gen) flips exactly the entries whose remote fact that checkpoint contains.

#include "dataset_types.hpp"
#include "error.hpp"

#include <ankerl/unordered_dense.h>

#include <cstdint>
#include <vector>

namespace dew::core
{

enum class blob_residency_state_t : uint8_t
{
  // local-only blobs have no entry; these are the tracked states:
  local_uploaded = 1,  // local bytes + committed-or-pending copy in the destination's final layout
  remote_uploaded = 2, // no local bytes; fetch from the destination's final layout via remote_id
  local_spilled = 3,   // local bytes + copy in a spill segment
  remote_spilled = 4,  // no local bytes; fetch from a spill segment via remote_id
};

struct blob_residency_entry_t
{
  uint64_t offset = 0; // key (file_id is always 0 in the cached backend)
  uint32_t size = 0;
  blob_residency_state_t state = blob_residency_state_t::local_uploaded;
  bool durable = false;             // the remote fact is part of a committed checkpoint
  uint16_t inflight_local_reads = 0; // blocks punch while > 0 (reads already past dispatch)
  uint32_t marked_generation = 0;   // generation at which the remote fact was recorded
  uint64_t remote_id = 0;           // uploader/spill locator (layout-defined; opaque here)
  // Intrusive LRU links (slab indices), meaningful only while state == local_uploaded.
  uint32_t lru_prev = k_invalid_index;
  uint32_t lru_next = k_invalid_index;

  static constexpr uint32_t k_invalid_index = 0xFFFFFFFFu;
};

class blob_residency_t
{
public:
  // ---- resident-bytes accounting (covers ALL local bytes, entry-bearing or not) ----
  void set_cap(uint64_t cap_bytes) { _cap = cap_bytes; } // 0 = unlimited
  uint64_t cap() const { return _cap; }
  void account_alloc(uint64_t bytes) { _resident_bytes += bytes; }
  void account_freed(uint64_t bytes) { _resident_bytes -= bytes < _resident_bytes ? bytes : _resident_bytes; }
  uint64_t resident_bytes() const { return _resident_bytes; }
  bool over_soft() const { return _cap && _resident_bytes > _cap - _cap / 8; }              // 87.5%
  bool over_hard(uint64_t incoming) const { return _cap && _resident_bytes + incoming > _cap; }

  // ---- additive remote facts (any time) ----
  // LOCAL -> local_uploaded, or *_spilled -> *_uploaded (the uploader shipped a spilled blob to
  // the final layout; its old spill remote_id is returned so the caller can deref the segment).
  // Returns previous remote_id if the blob was spilled, 0 otherwise.
  uint64_t mark_uploaded(uint64_t offset, uint32_t size, uint64_t remote_id);
  // LOCAL -> local_spilled (spill pass copied the local bytes out).
  void mark_spilled(uint64_t offset, uint32_t size, uint64_t remote_id);
  // Direct spill of an incoming write under pressure: born remote_spilled, no local bytes ever
  // (the allocation reserved the offset identity but resident bytes are NOT charged).
  void mark_spilled_remote(uint64_t offset, uint32_t size, uint64_t remote_id);

  // ---- checkpoint barrier ----
  // Take the snapshot boundary: returns the generation the checkpoint being built contains and
  // ADVANCES the counter, so any remote fact recorded after this call belongs to the NEXT
  // checkpoint. Call when building the checkpoint payload; pass the returned value to
  // commit_durable() after the index write SUCCEEDS.
  uint32_t serialize_generation() { return _generation++; }
  void commit_durable(uint32_t generation_at_serialize);

  // ---- destructive transitions (call only under the durable rule) ----
  // local_uploaded -> remote_uploaded: drop the local bytes' accounting; the caller punches the
  // range afterwards. Refuses (returns false) unless durable and no in-flight local read.
  bool evict(uint64_t offset);
  // local_spilled -> remote_spilled, same contract as evict().
  bool drop_spilled_local(uint64_t offset);
  // Freed-blob path (the blob is logically gone, e.g. a superseded LOD): remove the entry entirely
  // and report what it was. The caller must NOT return the offset to the allocator -- through a
  // stale table a recycled offset would alias the old remote copy -- and reclaims local bytes with
  // a post-commit punch. Settles the resident-bytes accounting internally (like evict()).
  struct forgotten_blob_t
  {
    bool existed = false;
    bool punch_local = false; // had local bytes and no read in flight (safe to punch post-commit)
    bool spilled = false;     // deref the spill segment via remote_id
    uint64_t remote_id = 0;
  };
  forgotten_blob_t forget(uint64_t offset);

  // ---- read dispatch ----
  blob_residency_entry_t *find(uint64_t offset);
  const blob_residency_entry_t *find(uint64_t offset) const;
  bool has_local_bytes(const blob_residency_entry_t &e) const
  {
    return e.state == blob_residency_state_t::local_uploaded || e.state == blob_residency_state_t::local_spilled;
  }
  void touch(blob_residency_entry_t &e); // LRU MRU splice (local_uploaded only; no-op otherwise)
  void begin_local_read(blob_residency_entry_t &e) { e.inflight_local_reads++; }
  void end_local_read(blob_residency_entry_t &e)
  {
    if (e.inflight_local_reads)
      e.inflight_local_reads--;
  }

  // ---- eviction ----
  // LRU tail among local_uploaded entries that is durable with no in-flight reads; null if none.
  blob_residency_entry_t *pick_evict_victim();

  uint32_t entry_count() const { return uint32_t(_map.size()); }

  template <typename F>
  void for_each(F &&f) const
  {
    for (auto &kv : _map)
      f(_slab[kv.second]);
  }

  // ---- persistence (a metadata blob referenced from the superblock extras) ----
  // clean_shutdown: written true by the final checkpoint of an orderly teardown, false by every
  // mid-session checkpoint. On deserialize of a NOT-clean snapshot, every local_uploaded /
  // local_spilled entry is demoted to its remote_* state: a punch may have raced the crash and the
  // remote copy is the authoritative one (local bytes are unverifiable-cheaply).
  std::vector<uint8_t> serialize(bool clean_shutdown) const;
  [[nodiscard]] dew_error_t deserialize(const uint8_t *data, uint32_t size);

private:
  uint32_t get_or_create(uint64_t offset, uint32_t size);
  void lru_unlink(uint32_t idx);
  void lru_push_front(uint32_t idx);

  ankerl::unordered_dense::map<uint64_t, uint32_t> _map; // offset -> slab index
  std::vector<blob_residency_entry_t> _slab;
  std::vector<uint32_t> _free_slots;
  uint32_t _lru_head = blob_residency_entry_t::k_invalid_index;
  uint32_t _lru_tail = blob_residency_entry_t::k_invalid_index;
  uint64_t _resident_bytes = 0;
  uint64_t _cap = 0;
  uint32_t _generation = 1; // bumped per checkpoint serialize; entries record the gen of their remote fact
};

} // namespace dew::core
