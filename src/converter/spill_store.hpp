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

// Spill area for the cache tier: when the cache file is over its cap (and nothing uploaded is
// evictable) or the disk returns ENOSPC, NOT-YET-FINAL compressed blobs are packed into immutable
// SEGMENT objects under `spill/` in the destination bucket, refetched on demand with ranged GETs,
// and deleted once nothing references them. Per-blob PUTs would be wasteful; segments target
// tens of MB. A blob's spill locator (`remote_id` in the residency table) is
// (segment_seq << 32) | offset_in_segment; its size lives in the residency entry.
//
// Crash hygiene: vio's object stores have no LIST operation, so orphan segments from a crash are
// found via a tiny `spill/journal` object rewritten before every segment PUT with the set of
// not-yet-durable segment seqs. On open, journaled segments not referenced by the committed
// residency table are deleted. Segment deletion is DEFERRED to after a checkpoint commits (the
// same barrier as hole punching), via the dead-segment queue.
//
// Threading: single-threaded on the storage loop, like the rest of the backend tier.

#include "error.hpp"

#include <vio/objstore/object_store.h>
#include <vio/task.h>

#include <ankerl/unordered_dense.h>

#include <coroutine>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace points::converter
{

// FIFO gate serializing whole spill operations on the (single-threaded) storage loop. Coroutines
// still interleave at co_await points, so two concurrent spill_blob calls (a spill pass + a hard-cap
// divert) could otherwise both mutate the open segment around a suspended journal/segment PUT --
// appending into a buffer that a suspended flush() is about to clear loses the appended bytes.
// Not thread-safe by design; everything runs on one loop.
class coro_gate_t
{
public:
  struct lock_t // RAII: releases on scope exit, covering every co_return path
  {
    explicit lock_t(coro_gate_t *g)
      : _gate(g)
    {
    }
    lock_t(lock_t &&other) noexcept
      : _gate(other._gate)
    {
      other._gate = nullptr;
    }
    lock_t(const lock_t &) = delete;
    lock_t &operator=(const lock_t &) = delete;
    lock_t &operator=(lock_t &&) = delete;
    ~lock_t()
    {
      if (_gate)
        _gate->unlock();
    }

  private:
    coro_gate_t *_gate;
  };
  struct awaiter_t
  {
    coro_gate_t &gate;
    [[nodiscard]] bool await_ready() const
    {
      return !gate._locked;
    }
    void await_suspend(std::coroutine_handle<> handle)
    {
      gate._waiters.push_back(handle);
    }
    [[nodiscard]] lock_t await_resume() const
    {
      gate._locked = true;
      return lock_t{&gate};
    }
  };
  [[nodiscard]] awaiter_t lock()
  {
    return awaiter_t{*this};
  }

private:
  void unlock()
  {
    if (_waiters.empty())
    {
      _locked = false;
      return;
    }
    auto handle = _waiters.front();
    _waiters.erase(_waiters.begin());
    // _locked stays true: the gate is handed to the next waiter, whose await_resume re-asserts it.
    handle.resume();
  }

  bool _locked = false;
  std::vector<std::coroutine_handle<>> _waiters;
};

inline uint32_t spill_segment_of(uint64_t remote_id)
{
  return uint32_t(remote_id >> 32);
}
inline uint32_t spill_offset_of(uint64_t remote_id)
{
  return uint32_t(remote_id & 0xFFFFFFFFu);
}

class spill_store_t
{
public:
  spill_store_t(vio::objstore::io_manager_t &io, std::string prefix, uint32_t segment_target_bytes = 48u << 20);

  // Append `size` bytes to the open segment buffer; flushes (journal PUT + segment PUT) when the
  // buffer reaches the target. Returns the blob's spill locator. The bytes are already compressed.
  vio::task_t<points_error_t> spill_blob(const uint8_t *data, uint32_t size, uint64_t &remote_id_out);
  // PUT the open segment (if non-empty). Call before a checkpoint serializes the residency table,
  // so every persisted locator is backed by a durable object.
  vio::task_t<points_error_t> flush();

  // Ranged GET of one spilled blob.
  vio::task_t<points_error_t> read(uint64_t remote_id, uint8_t *dst, uint32_t size);

  // Live tracking: +1 per residency entry referencing the segment. Recomputed on open by the
  // backend walking the restored residency table (add_live per spilled entry).
  void add_live(uint64_t remote_id);
  // -1; a segment whose count hits zero queues for deletion at the next post-checkpoint sweep.
  void deref(uint64_t remote_id);
  // Delete queued dead segments + rewrite the journal without now-durable ones. Call AFTER a
  // checkpoint commit (the destructive barrier).
  vio::task_t<points_error_t> sweep_after_checkpoint();

  // Orphan GC on open: delete journaled segments the committed residency table doesn't reference.
  // Call after the backend restored the residency table and replayed add_live.
  vio::task_t<points_error_t> gc_orphans();

  uint32_t next_segment_seq() const { return _next_seq; }
  void restore_next_segment_seq(uint32_t seq) { _next_seq = seq > _next_seq ? seq : _next_seq; }
  uint64_t bytes_spilled() const { return _bytes_spilled; }

private:
  std::string segment_name(uint32_t seq) const;
  std::string journal_name() const;
  vio::task_t<points_error_t> write_journal();
  vio::task_t<points_error_t> flush_locked(); // body of flush(); caller holds _gate

  coro_gate_t _gate; // serializes spill_blob / flush / sweep (see coro_gate_t)
  vio::objstore::io_manager_t &_io;
  std::string _prefix;
  uint32_t _segment_target_bytes;
  // Starts at 1: locator 0 (= seq 0, offset 0) must never exist, because remote_id == 0 is the
  // "no spill reference" sentinel in the residency table (a zero locator would silently skip its
  // segment deref when the blob later uploads -- leaking the segment).
  uint32_t _next_seq = 1;

  // Open segment being assembled.
  std::vector<uint8_t> _open_buffer;
  uint32_t _open_seq = 0;
  bool _open_journaled = false;

  // seq -> live blob count for durable segments; zero-count seqs move to _dead_segments.
  ankerl::unordered_dense::map<uint32_t, uint32_t> _live_counts;
  std::vector<uint32_t> _dead_segments;      // deleted at the next post-checkpoint sweep
  std::vector<uint32_t> _undurable_segments; // PUT but not yet covered by a committed checkpoint (journaled)

  uint64_t _bytes_spilled = 0;
};

} // namespace points::converter
