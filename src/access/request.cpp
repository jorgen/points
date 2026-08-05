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

#include "dataset_impl.hpp"

#include "compressor.hpp"
#include "format_util.hpp"

#include <cstring>

namespace dew::access
{

void request_impl_t::cancel()
{
  auto expected = dew_request_pending;
  if (status.compare_exchange_strong(expected, dew_request_canceled))
    wait_cond.notify_all();
}

void request_impl_t::finish(dew_request_status_t terminal)
{
  auto expected = dew_request_pending;
  // A cancel that got in first wins: it is already terminal and must stay that way.
  status.compare_exchange_strong(expected, terminal);
  {
    std::unique_lock<std::mutex> lock(wait_mutex);
  }
  wait_cond.notify_all();
}

namespace
{

position_format_t to_internal(dew_position_format_t f)
{
  switch (f)
  {
  case dew_position_r32_relative:
    return position_format_t::r32_relative;
  case dew_position_i32_grid:
    return position_format_t::i32_grid;
  case dew_position_r64_absolute:
  default:
    return position_format_t::r64_absolute;
  }
}

// Read one blob and hand back its decompressed bytes.
struct blob_bytes_t
{
  std::shared_ptr<read_request_t> request;
  dew_blob_t view{};
  dew_error_t error;
};

// Suspends instead of blocking: the read completes on the storage loop and resumes this coroutine
// back on `resume_loop` (the dataset's own loop).
//
// decompress_inline is still set, because the decompress then happens on whichever thread completed
// the read rather than being hopped to a pool -- which under wasm has no workers at all, so a hopped
// decompress would simply never run.
vio::task_t<blob_bytes_t> co_read_blob(blob_reader_t &reader, storage_location_t location, vio::event_loop_t &resume_loop)
{
  blob_bytes_t out;
  if (location.size == 0)
    co_return out; // absent slot; note offset == 0 is a VALID location, so never test that
  std::shared_ptr<read_request_t> request;
  co_await co_read(reader, location, read_options_t{false, true, {}}, resume_loop, request);
  out.request = request;
  out.error = request->error;
  out.view = request->buffer_info;
  co_return out;
}

} // namespace

// Execute a region request end to end: walk to a converged node set, then for each node read the
// position blob plus each requested attribute, decode, optionally clip, and append to the
// concatenated output buffers.
//
// Runs as a coroutine on the dataset's own loop, so the caller's thread is never blocked. Reads are
// still issued one at a time -- overlapping them is the next step, and this one only has to prove
// the suspend/resume path against results that must not change.
vio::task_t<bool> run_region_request(dataset_impl_t &dataset, const region_job_t &spec, request_impl_t &request)
{
  region_query_t query;
  for (int i = 0; i < 3; i++)
  {
    query.box.min[i] = spec.box_min[i];
    query.box.max[i] = spec.box_max[i];
  }
  query.whole_dataset = (spec.box_min[0] >= spec.box_max[0] && spec.box_min[1] >= spec.box_max[1] && spec.box_min[2] >= spec.box_max[2]);
  switch (spec.lod_mode)
  {
  case dew_lod_level:
    query.lod_mode = lod_mode_t::level;
    query.lod = spec.lod;
    break;
  case dew_lod_point_budget:
    query.lod_mode = lod_mode_t::point_budget;
    query.max_points = spec.max_points;
    break;
  case dew_lod_full:
  default:
    query.lod_mode = lod_mode_t::full;
    break;
  }

  const uint32_t attribute_count = uint32_t(spec.attribute_names.size());

  region_result_t walked;
  if (!dataset.walk_to_convergence(query, walked))
  {
    request.error = dataset.error.code ? dataset.error : dew_error_t{1, "region walk failed"};
    co_return false;
  }

  const auto position_format = to_internal(spec.position_format);
  const uint32_t position_stride_bytes = position_stride(position_format);

  // Buffer 0 is always the positions; requested attributes follow in the order given.
  request.buffers.clear();
  request.buffers.resize(size_t(attribute_count) + 1);
  auto &positions = request.buffers[0];
  positions.name = "xyz";
  positions.stride = position_stride_bytes;
  switch (position_format)
  {
  case position_format_t::r64_absolute:
    positions.type = dew_type_r64;
    break;
  case position_format_t::r32_relative:
    positions.type = dew_type_r32;
    break;
  case position_format_t::i32_grid:
    positions.type = dew_type_i32;
    break;
  }
  positions.components = dew_components_3;

  const auto &names = spec.attribute_names;

  for (const auto &node : walked.nodes)
  {
    if (request.status.load(std::memory_order_acquire) == dew_request_canceled)
      co_return false;
    if (node.point_count.data == 0)
      continue;

    const tree_t *tree = dataset.registry.get(node.tree_id);
    if (!tree)
      continue;

    // Slot 0 of a storage unit is a storage_header_t followed by the morton codes.
    const auto position_location = tree->storage_map.location(node.input_id, 0);
    auto position_blob = co_await co_read_blob(*dataset.reader, position_location, dataset.loop_thread.event_loop());
    if (position_blob.error.code != 0)
    {
      request.error = position_blob.error;
      co_return false;
    }
    if (!position_blob.request)
      continue;

    storage_header_t header;
    dew_blob_t point_data;
    dew_error_t split_error;
    if (!deserialize_points(position_blob.view, header, point_data, split_error))
    {
      request.error = split_error;
      co_return false;
    }

    const uint32_t offset = node.offset_in_subset.data;
    const uint32_t count = node.point_count.data;
    if (uint64_t(offset) + count > header.point_count)
      continue; // subset does not fit the stored unit; skip rather than read out of bounds

    const uint32_t src_stride = uint32_t(size_for_format(header.point_format.type, header.point_format.components));
    const auto *src = static_cast<const uint8_t *>(point_data.data) + uint64_t(offset) * src_stride;

    const size_t base = positions.data.size();
    positions.data.resize(base + size_t(count) * position_stride_bytes);
    double origin[3] = {0, 0, 0};
    if (!decode_positions(src, count * src_stride, count, header.point_format, header.morton_min, header.lod_span, dataset.registry.tree_config, position_format,
                          positions.data.data() + base, uint64_t(count) * position_stride_bytes, origin))
    {
      request.error = {1, "failed to decode node positions"};
      co_return false;
    }

    // Each requested attribute, in the same order, from this node's own attribute set. A node whose
    // set lacks the attribute (slimmed LOD nodes drop the non-visual ones) contributes zeros, so all
    // buffers stay index-aligned with the positions.
    std::vector<attribute_span_t> spans;
    spans.reserve(attribute_count);
    std::vector<size_t> attribute_bases;
    attribute_bases.reserve(attribute_count);
    for (uint32_t a = 0; a < attribute_count; a++)
    {
      auto &out = request.buffers[a + 1];
      const auto index = dataset.attributes.get_attribute_index(node.attributes_id, names[a]);
      if (out.stride == 0 && index.index >= 0)
      {
        out.name = names[a];
        out.type = index.format.type;
        out.components = index.format.components;
        out.stride = uint32_t(size_for_format(index.format.type, index.format.components));
      }
      const size_t attribute_base = out.data.size();
      attribute_bases.push_back(attribute_base);
      if (out.stride == 0)
      {
        spans.push_back({nullptr, 0});
        continue;
      }
      out.data.resize(attribute_base + size_t(count) * out.stride);

      if (index.index >= 0)
      {
        const auto location = tree->storage_map.location(node.input_id, index.index);
        auto blob = co_await co_read_blob(*dataset.reader, location, dataset.loop_thread.event_loop());
        if (blob.error.code == 0 && blob.request)
        {
          const auto *attribute_src = static_cast<const uint8_t *>(blob.view.data) + uint64_t(offset) * out.stride;
          if (uint64_t(offset + count) * out.stride <= blob.view.size)
            memcpy(out.data.data() + attribute_base, attribute_src, size_t(count) * out.stride);
        }
      }
      spans.push_back({out.data.data() + attribute_base, out.stride});
    }

    uint32_t kept = count;
    if (spec.clip_mode == dew_clip_point && !query.whole_dataset && !node.fully_inside)
    {
      kept = clip_to_box(positions.data.data() + base, position_format, origin, dataset.registry.tree_config.scale, count, spec.box_min, spec.box_max, spans.data(), uint32_t(spans.size()));
      positions.data.resize(base + size_t(kept) * position_stride_bytes);
      for (uint32_t a = 0; a < attribute_count; a++)
      {
        auto &out = request.buffers[a + 1];
        if (out.stride)
          out.data.resize(attribute_bases[a] + size_t(kept) * out.stride);
      }
    }
    if (kept == 0)
      continue;

    dew_result_node_t result_node{};
    result_node.tree_id = node.tree_id.data;
    result_node.level = node.level;
    result_node.index = node.index;
    result_node.lod = node.lod;
    result_node.first_point = request.point_count;
    result_node.point_count = kept;
    for (int i = 0; i < 3; i++)
      result_node.position_offset[i] = origin[i];
    result_node.is_leaf = node.is_leaf ? 1 : 0;
    result_node.is_lod = node.is_lod ? 1 : 0;
    request.nodes.push_back(result_node);
    request.point_count += kept;
  }

  co_return true;
}

// Spawn the request on the dataset's own loop and return at once.
//
// The job and the request shared_ptr are passed BY VALUE into the coroutine, never captured by a
// coroutine lambda: a lambda's captures live in its closure, which is destroyed after the first
// suspension, so anything captured would dangle on resume. object_backend.cpp and dew_wasm_api.cpp
// both carry the same note.
void dataset_impl_t::spawn_region_request(region_job_t job, std::shared_ptr<dew_request_t> request)
{
  auto *dataset = this;
  loop_thread.event_loop().run_in_loop([dataset, job = std::move(job), request]() mutable {
    [](dataset_impl_t *ds, region_job_t j, std::shared_ptr<dew_request_t> r) -> vio::detached_task_t {
      const bool ok = co_await run_region_request(*ds, j, *r);
      r->finish(ok ? dew_request_completed : dew_request_failed);
      // Queue for delivery and raise the wake. The callback itself runs later, on the host thread.
      ds->publish(r);
    }(dataset, std::move(job), request);
  });
}

} // namespace dew::access
