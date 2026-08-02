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
#include "blob_residency.hpp"

#include "memory_writer.hpp"

#include <cassert>
#include <cstring>

namespace dew::converter
{

static constexpr uint32_t k_residency_magic = 0x31534252u; // 'RBS1' little-endian

uint32_t blob_residency_t::get_or_create(uint64_t offset, uint32_t size)
{
  auto it = _map.find(offset);
  if (it != _map.end())
  {
    assert(_slab[it->second].size == size);
    return it->second;
  }
  uint32_t idx;
  if (!_free_slots.empty())
  {
    idx = _free_slots.back();
    _free_slots.pop_back();
    _slab[idx] = {};
  }
  else
  {
    idx = uint32_t(_slab.size());
    _slab.emplace_back();
  }
  auto &e = _slab[idx];
  e.offset = offset;
  e.size = size;
  _map[offset] = idx;
  return idx;
}

void blob_residency_t::lru_unlink(uint32_t idx)
{
  auto &e = _slab[idx];
  if (e.lru_prev != blob_residency_entry_t::k_invalid_index)
    _slab[e.lru_prev].lru_next = e.lru_next;
  else if (_lru_head == idx)
    _lru_head = e.lru_next;
  if (e.lru_next != blob_residency_entry_t::k_invalid_index)
    _slab[e.lru_next].lru_prev = e.lru_prev;
  else if (_lru_tail == idx)
    _lru_tail = e.lru_prev;
  e.lru_prev = blob_residency_entry_t::k_invalid_index;
  e.lru_next = blob_residency_entry_t::k_invalid_index;
}

void blob_residency_t::lru_push_front(uint32_t idx)
{
  auto &e = _slab[idx];
  e.lru_prev = blob_residency_entry_t::k_invalid_index;
  e.lru_next = _lru_head;
  if (_lru_head != blob_residency_entry_t::k_invalid_index)
    _slab[_lru_head].lru_prev = idx;
  _lru_head = idx;
  if (_lru_tail == blob_residency_entry_t::k_invalid_index)
    _lru_tail = idx;
}

uint64_t blob_residency_t::mark_uploaded(uint64_t offset, uint32_t size, uint64_t remote_id)
{
  uint32_t idx = get_or_create(offset, size);
  auto &e = _slab[idx];
  uint64_t previous_spill_id = 0;
  switch (e.state)
  {
  case blob_residency_state_t::local_spilled:
    previous_spill_id = e.remote_id;
    e.state = blob_residency_state_t::local_uploaded;
    break;
  case blob_residency_state_t::remote_spilled:
    previous_spill_id = e.remote_id;
    e.state = blob_residency_state_t::remote_uploaded;
    break;
  case blob_residency_state_t::local_uploaded:
  case blob_residency_state_t::remote_uploaded:
    // Fresh entry from get_or_create lands here as local_uploaded (its zero-init default).
    break;
  }
  e.remote_id = remote_id;
  e.durable = false;
  e.marked_generation = _generation;
  if (e.state == blob_residency_state_t::local_uploaded)
  {
    lru_unlink(idx);
    lru_push_front(idx); // enters (or refreshes) the evictable set at MRU
  }
  return previous_spill_id;
}

void blob_residency_t::mark_spilled(uint64_t offset, uint32_t size, uint64_t remote_id)
{
  uint32_t idx = get_or_create(offset, size);
  auto &e = _slab[idx];
  assert(e.state == blob_residency_state_t::local_uploaded || e.remote_id == 0); // LOCAL (fresh) expected
  lru_unlink(idx); // spilled blobs are reclaimed by the spill flow, not the upload-LRU
  e.state = blob_residency_state_t::local_spilled;
  e.remote_id = remote_id;
  e.durable = false;
  e.marked_generation = _generation;
}

void blob_residency_t::mark_spilled_remote(uint64_t offset, uint32_t size, uint64_t remote_id)
{
  uint32_t idx = get_or_create(offset, size);
  auto &e = _slab[idx];
  lru_unlink(idx);
  e.state = blob_residency_state_t::remote_spilled;
  e.remote_id = remote_id;
  e.durable = false;
  e.marked_generation = _generation;
}

void blob_residency_t::commit_durable(uint32_t generation_at_serialize)
{
  // Flip exactly the remote facts the just-committed checkpoint contains. Facts recorded after the
  // snapshot boundary (marked_generation > generation_at_serialize) stay pending for the next one.
  for (auto &kv : _map)
  {
    auto &e = _slab[kv.second];
    if (!e.durable && e.marked_generation <= generation_at_serialize)
      e.durable = true;
  }
}

bool blob_residency_t::evict(uint64_t offset)
{
  auto it = _map.find(offset);
  if (it == _map.end())
    return false;
  auto &e = _slab[it->second];
  if (e.state != blob_residency_state_t::local_uploaded || !e.durable || e.inflight_local_reads)
    return false;
  lru_unlink(it->second);
  e.state = blob_residency_state_t::remote_uploaded;
  account_freed(e.size);
  return true;
}

blob_residency_t::forgotten_blob_t blob_residency_t::forget(uint64_t offset)
{
  forgotten_blob_t out;
  auto it = _map.find(offset);
  if (it == _map.end())
    return out;
  const uint32_t idx = it->second;
  auto &e = _slab[idx];
  out.existed = true;
  out.spilled = e.state == blob_residency_state_t::local_spilled || e.state == blob_residency_state_t::remote_spilled;
  out.remote_id = e.remote_id;
  const bool local = has_local_bytes(e);
  out.punch_local = local && e.inflight_local_reads == 0;
  if (local)
    account_freed(e.size);
  lru_unlink(idx);
  _map.erase(it);
  _slab[idx] = {};
  _free_slots.push_back(idx);
  return out;
}

bool blob_residency_t::drop_spilled_local(uint64_t offset)
{
  auto it = _map.find(offset);
  if (it == _map.end())
    return false;
  auto &e = _slab[it->second];
  if (e.state != blob_residency_state_t::local_spilled || !e.durable || e.inflight_local_reads)
    return false;
  e.state = blob_residency_state_t::remote_spilled;
  account_freed(e.size);
  return true;
}

blob_residency_entry_t *blob_residency_t::find(uint64_t offset)
{
  auto it = _map.find(offset);
  return it == _map.end() ? nullptr : &_slab[it->second];
}

const blob_residency_entry_t *blob_residency_t::find(uint64_t offset) const
{
  auto it = _map.find(offset);
  return it == _map.end() ? nullptr : &_slab[it->second];
}

void blob_residency_t::touch(blob_residency_entry_t &e)
{
  if (e.state != blob_residency_state_t::local_uploaded)
    return;
  uint32_t idx = uint32_t(&e - _slab.data());
  lru_unlink(idx);
  lru_push_front(idx);
}

blob_residency_entry_t *blob_residency_t::pick_evict_victim()
{
  for (uint32_t idx = _lru_tail; idx != blob_residency_entry_t::k_invalid_index; idx = _slab[idx].lru_prev)
  {
    auto &e = _slab[idx];
    if (e.durable && e.inflight_local_reads == 0)
      return &e;
  }
  return nullptr;
}

std::vector<uint8_t> blob_residency_t::serialize(bool clean_shutdown) const
{
  // Header: magic, clean flag, cap, resident bytes, generation, entry count. Entries in slab-walk
  // order via the map (LRU order is NOT persisted: after a reopen every local_uploaded entry
  // re-enters the LRU in deserialize order -- recency is a heuristic, not state worth bytes).
  uint32_t size = 0;
  size += sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint64_t) * 2 + sizeof(uint32_t) * 2;
  size += uint32_t(_map.size()) * uint32_t(sizeof(uint64_t) * 2 + sizeof(uint32_t) + sizeof(uint8_t) * 2);

  std::vector<uint8_t> out(size);
  uint8_t *ptr = out.data();
  uint8_t *end = ptr + out.size();
  bool ok = write_memory(ptr, end, k_residency_magic);
  ok = ok && write_memory(ptr, end, uint8_t(clean_shutdown ? 1 : 0));
  ok = ok && write_memory(ptr, end, _cap);
  ok = ok && write_memory(ptr, end, _resident_bytes);
  ok = ok && write_memory(ptr, end, _generation);
  ok = ok && write_memory(ptr, end, uint32_t(_map.size()));
  for (auto &kv : _map)
  {
    auto &e = _slab[kv.second];
    ok = ok && write_memory(ptr, end, e.offset);
    ok = ok && write_memory(ptr, end, e.remote_id);
    ok = ok && write_memory(ptr, end, e.size);
    ok = ok && write_memory(ptr, end, uint8_t(e.state));
    ok = ok && write_memory(ptr, end, uint8_t(e.durable ? 1 : 0));
  }
  assert(ok && ptr == end);
  if (!ok)
    return {};
  return out;
}

dew_error_t blob_residency_t::deserialize(const uint8_t *data, uint32_t size)
{
  const dew_error_t invalid = {1, "Invalid blob residency data"};
  const uint8_t *ptr = data;
  const uint8_t *end = data + size;
  uint32_t magic = 0;
  if (!read_memory(ptr, end, magic) || magic != k_residency_magic)
    return invalid;
  uint8_t clean = 0;
  uint32_t count = 0;
  bool ok = read_memory(ptr, end, clean);
  ok = ok && read_memory(ptr, end, _cap);
  ok = ok && read_memory(ptr, end, _resident_bytes);
  ok = ok && read_memory(ptr, end, _generation);
  ok = ok && read_memory(ptr, end, count);
  if (!ok)
    return invalid;

  _map.clear();
  _slab.clear();
  _free_slots.clear();
  _lru_head = _lru_tail = blob_residency_entry_t::k_invalid_index;
  _slab.reserve(count);
  for (uint32_t i = 0; i < count; i++)
  {
    blob_residency_entry_t e;
    uint8_t state = 0;
    uint8_t durable = 0;
    ok = read_memory(ptr, end, e.offset);
    ok = ok && read_memory(ptr, end, e.remote_id);
    ok = ok && read_memory(ptr, end, e.size);
    ok = ok && read_memory(ptr, end, state);
    ok = ok && read_memory(ptr, end, durable);
    if (!ok || state < uint8_t(blob_residency_state_t::local_uploaded) || state > uint8_t(blob_residency_state_t::remote_spilled))
      return invalid;
    e.state = blob_residency_state_t(state);
    // Every persisted entry's remote fact was, by construction, part of this committed snapshot.
    e.durable = true;
    (void)durable;
    if (!clean)
    {
      // Unclean shutdown: a punch may have raced the crash after this snapshot was written, so the
      // local bytes of tracked entries are unverifiable -- demote to the remote copy, which is
      // authoritative. (Untracked LOCAL blobs are never punched, so they need no demotion.)
      if (e.state == blob_residency_state_t::local_uploaded)
      {
        e.state = blob_residency_state_t::remote_uploaded;
        account_freed(e.size);
      }
      else if (e.state == blob_residency_state_t::local_spilled)
      {
        e.state = blob_residency_state_t::remote_spilled;
        account_freed(e.size);
      }
    }
    uint32_t idx = uint32_t(_slab.size());
    _slab.push_back(e);
    _map[e.offset] = idx;
    if (e.state == blob_residency_state_t::local_uploaded)
      lru_push_front(idx);
  }
  return {};
}

} // namespace dew::converter
