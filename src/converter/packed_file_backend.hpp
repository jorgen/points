/************************************************************************
** dewfall - point cloud management software.
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

#include "blob_manager.hpp"
#include "blob_residency.hpp"
#include "index_format.hpp"
#include "spill_store.hpp"
#include "storage_backend.hpp"

#include <vio/operation/file.h>

#include <ankerl/unordered_dense.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace dew::converter
{

// The single-file (packed) storage backend: all blobs packed into one file's linear byte space by
// free_blob_manager_t, with the 128-byte index/superblock at offset 0 rewritten in place and fsync'd
// on every checkpoint. This is the historical behavior of storage_handler, extracted verbatim.
//
// CACHE TIER (optional, dormant by default): enable_cache_tier() attaches a blob_residency_t that
// tracks which data blobs also exist remotely (uploaded to the destination / spilled) and accounts
// resident bytes against a cap. With the tier DISABLED every code path below is byte-for-byte the
// historical packed behavior -- the tier adds: resident-byte accounting in allocate_blob, a
// residency-table metadata blob + superblock extras in write_index (with the durable-bit commit
// after the superblock lands), and residency restore in read_index. Eviction/spill/remote reads
// build on this in the pressure tier (see blob_residency.hpp for the state machine + rules).
class packed_file_backend_t : public storage_backend_t
{
public:
  packed_file_backend_t(std::string file_name, vio::event_loop_t &event_loop, dew_error_t &error);
  ~packed_file_backend_t() override;

  bool is_packed_file() const override
  {
    return true;
  }

  // Attach the cache tier (call before open_for_write/read_index). cap_bytes 0 = track only, no cap.
  void enable_cache_tier(uint64_t cap_bytes);
  bool cache_tier_enabled() const { return _residency != nullptr; }
  blob_residency_t *residency() { return _residency.get(); }
  // Attach the spill area (requires the cache tier): not-yet-uploaded data blobs can then be packed
  // into segment objects under `prefix` on `io` when the cap/disk forces them out. Call before
  // open_for_write/read_index (gc/replay run at read_index).
  void enable_spill(std::unique_ptr<vio::objstore::io_manager_t> io, std::string prefix, uint32_t segment_target_bytes = 48u << 20);
  spill_store_t *spill() { return _spill.get(); }
  // Evict durable LRU victims (punching their bytes) until comfortably under the cap. Runs inline
  // on the storage loop; punches are microsecond syscalls. Returns bytes reclaimed.
  uint64_t run_eviction_pass();
  // True when the tier is over its soft watermark and eviction cannot reclaim (nothing durable):
  // the caller should request a checkpoint so pending remote facts become evictable.
  bool wants_checkpoint() const override;
  void maybe_evict() override
  {
    if (!_residency || !_residency->over_soft())
      return;
    run_eviction_pass();
    // Eviction couldn't buy enough headroom (nothing uploaded/durable): push EXISTING local data
    // blobs out to the spill area, newest (highest-morton, needed-latest by LOD) first.
    if (_spill && _residency->over_soft() && !_spill_pass_running)
    {
      _spill_pass_running = true;
      [](packed_file_backend_t *self) -> vio::detached_task_t
      {
        co_await self->run_spill_pass();
        self->_spill_pass_running = false;
      }(this);
    }
  }
  // After read_index restored the residency table: replay spilled-blob liveness into the spill
  // store and GC crash-orphaned segments. Async (object IO); run on the storage loop.
  vio::task_t<dew_error_t> spill_bootstrap();
  void note_blob_uploaded(uint64_t offset, uint32_t size, uint64_t remote_id) override
  {
    if (!_residency)
      return;
    // A spilled blob shipped to the final layout releases its spill-segment reference.
    uint64_t old_spill_id = _residency->mark_uploaded(offset, size, remote_id);
    if (old_spill_id && _spill)
      _spill->deref(old_spill_id);
  }
  // Copy existing LOCAL data blobs (descending offset ≈ highest morton ≈ consumed latest) into
  // spill segments until under the low watermark. Local bytes stay readable until their spill
  // fact turns durable at the next checkpoint, after which they are punched.
  vio::task_t<dew_error_t> run_spill_pass();
  // The next checkpoint is the orderly-teardown one: its residency table is marked clean, so a
  // reopen keeps local_* states instead of demoting them (see blob_residency_t::deserialize).
  void set_clean_shutdown_next_checkpoint() { _clean_shutdown_next_checkpoint = true; }
  // uuid tying this cache to its destination dataset (written into the superblock extras).
  void set_dataset_uuid(const uint8_t (&uuid)[16]) { memcpy(_dataset_uuid, uuid, sizeof(_dataset_uuid)); }
  const uint8_t (&dataset_uuid() const)[16] { return _dataset_uuid; }

  [[nodiscard]] bool exists() const override;
  [[nodiscard]] dew_error_t open_for_write(bool truncate) override;
  [[nodiscard]] dew_error_t read_index(index_load_t &out) override;
  [[nodiscard]] dew_error_t restore_allocator(const std::unique_ptr<uint8_t[]> &data, uint32_t size) override;
  void allocate_blob(uint32_t size, blob_kind_t kind, storage_location_t &out) override;
  vio::task_t<dew_error_t> write_allocated(storage_location_t location, std::shared_ptr<uint8_t[]> data) override;
  vio::task_t<dew_error_t> read_blob(storage_location_t location, uint8_t *dst, uint32_t &bytes_read) override;
  vio::task_t<dew_error_t> write_index(checkpoint_t checkpoint) override;

private:
  std::string _file_name;
  vio::event_loop_t &_event_loop;
  std::optional<vio::auto_close_file_t> _file;
  bool _file_exists = false;
  uint32_t _serialized_index_size = k_serialized_index_size;
  free_blob_manager_t _blob_manager;

  // Metadata blob locations from the previously committed index, freed at the next checkpoint.
  // NB: matching the original behavior, read_index records only stats/perf here; attributes/free-blobs
  // stay {0} after a reopen (they are only tracked once written by this session's first checkpoint).
  storage_location_t _attributes_location;
  storage_location_t _blobs_location;
  storage_location_t _stats_location;
  storage_location_t _perf_stats_location;
  storage_location_t _tree_registry_location; // previous registry blob, freed at the next checkpoint
  storage_location_t _residency_location;     // previous residency-table blob, freed at the next checkpoint

  // Cache tier (null = classic packed behavior).
  std::unique_ptr<blob_residency_t> _residency;
  std::unique_ptr<vio::objstore::io_manager_t> _spill_io;
  std::unique_ptr<spill_store_t> _spill;
  bool _clean_shutdown_next_checkpoint = false;
  uint8_t _dataset_uuid[16] = {};
  // Punch capability, resolved lazily on the first eviction attempt: unknown(-1) / no(0) / yes(1).
  int _punch_supported = -1;
  // Data-blob offsets allocated while over the hard cap (or after an ENOSPC): their write_allocated
  // diverts to the spill area instead of touching the disk. Filled under _mutex, consumed on the loop.
  ankerl::unordered_dense::set<uint64_t> _divert_on_write;
  // Data-blob offsets allocated but whose write_allocated has not completed. The spill pass (a
  // storage-loop coroutine interleaving with the write coroutines at co_await points) must not
  // read them from the file -- it would pack the pre-write garbage into a segment.
  ankerl::unordered_dense::set<uint64_t> _write_pending;
  // Every DATA blob allocation (offset,size), append-only; the spill-existing pass walks it from
  // the back (descending offset). 16 bytes per blob. Guarded by _mutex on append.
  std::vector<std::pair<uint64_t, uint32_t>> _data_blobs;
  bool _spill_pass_running = false;

  std::mutex _mutex; // guards _blob_manager during allocate_blob (and _divert_on_write)
};

} // namespace dew::converter
