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
#include "spill_store.hpp"

#include <cstdio>
#include <cstdlib>

#include "memory_writer.hpp"

#include <fmt/format.h>

#include <cassert>
#include <cstring>

namespace dew::core
{

static constexpr uint32_t k_journal_magic = 0x314c4a53u; // 'SJL1'

static dew_error_t from_vio(const vio::error_t &e)
{
  dew_error_t r;
  r.code = e.code;
  r.msg = e.msg;
  return r;
}

spill_store_t::spill_store_t(vio::objstore::io_manager_t &io, std::string prefix, uint32_t segment_target_bytes)
  : _io(io)
  , _prefix(std::move(prefix))
  , _segment_target_bytes(segment_target_bytes)
{
}

std::string spill_store_t::segment_name(uint32_t seq) const
{
  return fmt::format("{}seg_{:08x}", _prefix, seq);
}

std::string spill_store_t::journal_name() const
{
  return _prefix + "journal";
}

vio::task_t<dew_error_t> spill_store_t::write_journal()
{
  // The journal lists every segment that exists (or is about to exist) remotely but is NOT yet
  // referenced by a committed residency table: the open segment + the undurable ones. On-open GC
  // deletes journaled segments the committed table doesn't know about.
  uint32_t count = uint32_t(_undurable_segments.size()) + 1; // +1 for the open/incoming segment
  uint32_t size = uint32_t(sizeof(k_journal_magic) + sizeof(count) + count * sizeof(uint32_t));
  auto data = std::make_shared<uint8_t[]>(size);
  uint8_t *ptr = data.get();
  uint8_t *end = ptr + size;
  bool ok = write_memory(ptr, end, k_journal_magic);
  ok = ok && write_memory(ptr, end, count);
  for (auto seq : _undurable_segments)
    ok = ok && write_memory(ptr, end, seq);
  ok = ok && write_memory(ptr, end, _open_seq);
  assert(ok && ptr == end);
  if (!ok)
    co_return dew_error_t{1, "Failed to serialize spill journal"};
  auto r = co_await _io.write_object(journal_name(), std::move(data), size);
  if (!r.has_value())
    co_return from_vio(r.error());
  co_return dew_error_t{};
}

vio::task_t<dew_error_t> spill_store_t::spill_blob(const uint8_t *data, uint32_t size, uint64_t &remote_id_out)
{
  // Whole-operation gate: another spill_blob interleaving at the journal/flush suspension points
  // would append into a buffer a suspended flush is about to clear (bytes lost -> short reads).
  auto gate_lock = co_await _gate.lock();
  if (_open_buffer.empty())
  {
    _open_seq = _next_seq++;
    _open_journaled = false;
    _open_buffer.reserve(_segment_target_bytes);
  }
  if (!_open_journaled)
  {
    // Journal BEFORE the segment can exist remotely, so a crash between the segment PUT and the
    // next checkpoint leaves a GC-able record instead of an unfindable orphan (no list op).
    auto err = co_await write_journal();
    if (err.code != 0)
      co_return err;
    _open_journaled = true;
  }
  const uint32_t offset = uint32_t(_open_buffer.size());
  _open_buffer.insert(_open_buffer.end(), data, data + size);
  remote_id_out = (uint64_t(_open_seq) << 32) | offset;
  _bytes_spilled += size;
  _live_counts[_open_seq]++;

  if (_open_buffer.size() >= _segment_target_bytes)
    co_return co_await flush_locked();
  co_return dew_error_t{};
}

vio::task_t<dew_error_t> spill_store_t::flush()
{
  auto gate_lock = co_await _gate.lock();
  co_return co_await flush_locked();
}

vio::task_t<dew_error_t> spill_store_t::flush_locked()
{
  if (_open_buffer.empty())
    co_return dew_error_t{};
  const uint32_t size = uint32_t(_open_buffer.size());
  auto data = std::make_shared<uint8_t[]>(size);
  memcpy(data.get(), _open_buffer.data(), size);
  auto r = co_await _io.write_object(segment_name(_open_seq), std::move(data), size);
  if (!r.has_value())
    co_return from_vio(r.error());
  _undurable_segments.push_back(_open_seq);
  _open_buffer.clear();
  _open_buffer.shrink_to_fit();
  _open_journaled = false;
  co_return dew_error_t{};
}

vio::task_t<dew_error_t> spill_store_t::read(uint64_t remote_id, uint8_t *dst, uint32_t size)
{
  const uint32_t seq = spill_segment_of(remote_id);
  const uint32_t offset = spill_offset_of(remote_id);
  // Serve from the open (not yet PUT) segment when the blob is still buffered.
  if (!_open_buffer.empty() && seq == _open_seq)
  {
    if (uint64_t(offset) + size > _open_buffer.size())
      co_return dew_error_t{1, "Spill read out of open-segment bounds"};
    memcpy(dst, _open_buffer.data() + offset, size);
    co_return dew_error_t{};
  }
  vio::objstore::io_range_t range;
  range.offset = int64_t(offset);
  range.size = int64_t(size);
  auto r = co_await _io.read_object(segment_name(seq), dst, range);
  if (!r.has_value())
    co_return from_vio(r.error());
  if (r.value() != size)
    co_return dew_error_t{1, "Short spill segment read"};
  co_return dew_error_t{};
}

void spill_store_t::add_live(uint64_t remote_id)
{
  const uint32_t seq = spill_segment_of(remote_id);
  _live_counts[seq]++;
  restore_next_segment_seq(seq + 1);
}

void spill_store_t::deref(uint64_t remote_id)
{
  const uint32_t seq = spill_segment_of(remote_id);
  auto it = _live_counts.find(seq);
  assert(it != _live_counts.end());
  if (it == _live_counts.end())
    return;
  if (--it->second == 0)
  {
    _live_counts.erase(it);
    // Never delete the still-open segment; it is reused until flushed.
    if (!(seq == _open_seq && !_open_buffer.empty()))
      _dead_segments.push_back(seq);
  }
}

vio::task_t<dew_error_t> spill_store_t::sweep_after_checkpoint()
{
  // Under the gate: this rewrites/removes the journal, which must not interleave with a
  // spill_blob's own journal write.
  auto gate_lock = co_await _gate.lock();
  if (std::getenv("DEW_DEBUG_SPILL"))
    fprintf(stderr, "[spill] sweep: dead=%zu undurable=%zu live_segs=%zu open=%zu\n", _dead_segments.size(), _undurable_segments.size(), _live_counts.size(), _open_buffer.size());
  // The checkpoint that just committed references (via the residency table) every segment in
  // _undurable_segments that still has live blobs -- those are durable now and leave the journal.
  _undurable_segments.clear();
  dew_error_t first_error = {};
  for (auto seq : _dead_segments)
  {
    auto r = co_await _io.remove_object(segment_name(seq));
    if (!r.has_value() && first_error.code == 0)
      first_error = from_vio(r.error());
  }
  _dead_segments.clear();
  // Journal now only needs to cover the open segment (if any); rewrite it shrunk.
  if (!_open_buffer.empty())
  {
    auto err = co_await write_journal();
    if (err.code != 0 && first_error.code == 0)
      first_error = err;
  }
  else
  {
    auto r = co_await _io.remove_object(journal_name());
    if (!r.has_value() && first_error.code == 0)
      first_error = from_vio(r.error());
  }
  co_return first_error;
}

vio::task_t<dew_error_t> spill_store_t::gc_orphans()
{
  // On open: journaled segments not referenced by the restored residency table (i.e. absent from
  // _live_counts, which the backend replayed via add_live) are crash orphans -- delete them.
  auto info = co_await _io.object_info(journal_name());
  if (!info.has_value())
    co_return from_vio(info.error());
  if (!info.value().exists || info.value().size < 8)
    co_return dew_error_t{};
  std::vector<uint8_t> buffer(info.value().size);
  auto r = co_await _io.read_object(journal_name(), buffer.data(), {});
  if (!r.has_value())
    co_return from_vio(r.error());
  const uint8_t *ptr = buffer.data();
  const uint8_t *end = ptr + buffer.size();
  uint32_t magic = 0;
  uint32_t count = 0;
  if (!read_memory(ptr, end, magic) || magic != k_journal_magic || !read_memory(ptr, end, count))
    co_return dew_error_t{1, "Invalid spill journal"};
  dew_error_t first_error = {};
  for (uint32_t i = 0; i < count; i++)
  {
    uint32_t seq = 0;
    if (!read_memory(ptr, end, seq))
      co_return dew_error_t{1, "Invalid spill journal"};
    restore_next_segment_seq(seq + 1);
    if (_live_counts.contains(seq))
      continue; // referenced by the committed table -> keep
    auto rr = co_await _io.remove_object(segment_name(seq));
    if (!rr.has_value() && first_error.code == 0)
      first_error = from_vio(rr.error());
  }
  auto rj = co_await _io.remove_object(journal_name());
  if (!rj.has_value() && first_error.code == 0)
    first_error = from_vio(rj.error());
  co_return first_error;
}

} // namespace dew::core
