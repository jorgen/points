/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#ifdef __EMSCRIPTEN__

#include "worker_node_data_loader.hpp"

#include "native_node_data_loader.hpp" // native_load_request_t (the request payload the render pipeline builds)
#include "node_decode.hpp"             // loaded_node_impl_data_t

#include <cstring>

namespace dew::converter
{
using namespace dew::core;

namespace
{
// Copy a JS Uint8Array / ArrayBuffer (a decode reply buffer) into a wasm-owned buffer. Returns null + size 0
// for a null/undefined slot. HEAPU8 is re-fetched after the allocation because ALLOW_MEMORY_GROWTH can
// replace the heap's backing buffer when make_shared grows memory.
std::shared_ptr<uint8_t[]> copy_val_to_wasm(const emscripten::val &v, uint32_t &size_out)
{
  if (v.isNull() || v.isUndefined())
  {
    size_out = 0;
    return {};
  }
  // Normalize to a Uint8Array (a view over an ArrayBuffer, or a copy of a foreign typed array).
  emscripten::val u8 = emscripten::val::global("Uint8Array").new_(v);
  const uint32_t n = u8["length"].as<uint32_t>();
  if (n == 0)
  {
    size_out = 0;
    return {};
  }
  auto buf = std::make_shared<uint8_t[]>(n);
  // Copy JS -> wasm through a view over OUR buffer (dst.set(u8)), not via Module.HEAPU8: MODULARIZE builds do
  // not export HEAPU8, so val::module_property("HEAPU8") is undefined and .set() throws.
  emscripten::val dst(emscripten::typed_memory_view(n, buf.get()));
  dst.call<void>("set", u8);
  size_out = n;
  return buf;
}
} // namespace

bool decode_worker_pool_available()
{
  emscripten::val pool = emscripten::val::global("__dewDecodePool");
  return !pool.isNull() && !pool.isUndefined();
}

worker_node_data_loader_t::worker_node_data_loader_t(storage_handler_t &storage_handler)
  : _storage_handler(storage_handler)
  , _pool(emscripten::val::global("__dewDecodePool"))
{
}

render::load_handle_t worker_node_data_loader_t::request_load(const void *request_data, uint32_t request_size)
{
  native_load_request_t req;
  std::memcpy(&req, request_data, sizeof(req) < request_size ? sizeof(req) : request_size);

  const uint64_t handle = _next_handle++;
  pending_t p;
  p.tree_config = req.tree_config;
  p.want_salvage = req.want_salvage;
  for (int i = 0; i < 4; ++i)
  {
    p.format[i] = req.format[i];
    if (req.locations[i].size > 0)
      p.reads[i] = _storage_handler.read(req.locations[i], /*raw=*/true);
  }
  _pending.emplace(handle, std::move(p));
  return handle;
}

void worker_node_data_loader_t::drain_replies()
{
  // Single-threaded (wasm) main thread: the pool accumulates replies posted from the worker(s) and hands them
  // over as an array of {id, result}. Stamp each onto its pending entry (a late reply for a cancelled/erased
  // handle is simply dropped).
  emscripten::val replies = _pool.call<emscripten::val>("drain");
  if (replies.isNull() || replies.isUndefined())
    return;
  const uint32_t count = replies["length"].as<uint32_t>();
  for (uint32_t i = 0; i < count; ++i)
  {
    emscripten::val entry = replies[i];
    const uint64_t id = static_cast<uint64_t>(entry["id"].as<double>());
    auto it = _pending.find(id);
    if (it == _pending.end())
      continue;
    it->second.reply = entry["result"];
    it->second.phase = phase_t::ready;
  }
}

void worker_node_data_loader_t::post_to_worker(uint64_t id, pending_t &p)
{
  emscripten::val msg = emscripten::val::object();
  msg.set("treeScale", p.tree_config.scale);
  emscripten::val off = emscripten::val::array();
  off.set(0, p.tree_config.offset[0]);
  off.set(1, p.tree_config.offset[1]);
  off.set(2, p.tree_config.offset[2]);
  msg.set("treeOffset", off);

  emscripten::val formats = emscripten::val::array();
  emscripten::val buffers = emscripten::val::array();
  for (int i = 0; i < 4; ++i)
  {
    emscripten::val f = emscripten::val::object();
    f.set("type", static_cast<int>(p.format[i].type));
    f.set("components", static_cast<int>(p.format[i].components));
    formats.set(i, f);

    const auto &r = p.reads[i];
    if (r && r->buffer && r->buffer_info.size > 0)
      // A view into the wasm heap. The pool copies (slice) it into a Transferable before posting to a worker,
      // so the heap is never detached and these read buffers stay valid until get_data drops the pending entry.
      buffers.set(i, emscripten::val(emscripten::typed_memory_view(r->buffer_info.size, r->buffer.get())));
    else
      buffers.set(i, emscripten::val::null());
  }
  msg.set("formats", formats);
  msg.set("buffers", buffers);
  msg.set("wantSalvage", p.want_salvage);

  _pool.call<void>("post", static_cast<double>(id), msg);
  p.phase = phase_t::posted;
}

bool worker_node_data_loader_t::is_ready(render::load_handle_t handle)
{
  drain_replies();

  auto it = _pending.find(handle);
  if (it == _pending.end())
    return false;
  pending_t &p = it->second;

  if (p.phase == phase_t::reading)
  {
    bool all_done = true;
    for (const auto &r : p.reads)
      if (r && !r->_done) // wasm is single-threaded; the read completion runs on this same thread
        all_done = false;
    if (all_done)
      post_to_worker(handle, p);
  }
  return p.phase == phase_t::ready;
}

render::loaded_node_data_t worker_node_data_loader_t::get_data(render::load_handle_t handle)
{
  emscripten::val reply = emscripten::val::undefined();
  point_format_t format[4]{};
  {
    auto it = _pending.find(handle);
    if (it == _pending.end())
      return {};
    reply = it->second.reply;
    for (int i = 0; i < 4; ++i)
      format[i] = it->second.format[i];
    _pending.erase(it);
  }
  if (reply.isNull() || reply.isUndefined())
    return {};

  render::loaded_node_data_t out;
  out.point_count = reply["pointCount"].as<uint32_t>();
  out.has_lod_order = reply["hasLodOrder"].as<bool>();
  out.draw_type = static_cast<dew_draw_type_t>(reply["drawType"].as<int>());
  out.vertex_type = static_cast<dew_type_t>(reply["vertexType"].as<int>());
  out.vertex_components = static_cast<dew_components_t>(reply["vertexComponents"].as<int>());
  out.attribute_type = static_cast<dew_type_t>(reply["attributeType"].as<int>());
  out.attribute_components = static_cast<dew_components_t>(reply["attributeComponents"].as<int>());

  emscripten::val off = reply["offset"];
  out.offset[0] = off[0].as<double>();
  out.offset[1] = off[1].as<double>();
  out.offset[2] = off[2].as<double>();

  emscripten::val prefix = reply["prefixCount"];
  for (int i = 0; i < 64; ++i)
    out.prefix_count[i] = prefix[i].as<uint32_t>();

  auto impl = std::make_shared<loaded_node_impl_data_t>();
  impl->vertex_data = copy_val_to_wasm(reply["vertex"], out.vertex_data_size);
  impl->attribute_data = copy_val_to_wasm(reply["attribute"], out.attribute_data_size);
  impl->rep_level_data = copy_val_to_wasm(reply["repLevel"], out.rep_level_data_size);
  out.vertex_data = impl->vertex_data.get();
  out.attribute_data = impl->attribute_data.get();
  out.rep_level_data = impl->rep_level_data.get();

  // Salvage handler (leaves that may become virtual subnodes): rebuild a dyn_points_data_handler_t from the raw
  // decompressed points + attr blobs the worker shipped, deserializing exactly like the native read path's
  // is_done(). render_pipeline lifts impl->data_handler into node.resident_handler when it promotes the leaf.
  emscripten::val salvage_points = reply["salvagePoints"];
  if (!salvage_points.isNull() && !salvage_points.isUndefined())
  {
    auto handler = std::make_shared<dyn_points_data_handler_t>(format);

    auto rr0 = std::make_shared<read_request_t>();
    uint32_t pts_size = 0;
    rr0->buffer = copy_val_to_wasm(salvage_points, pts_size);
    rr0->buffer_info = dew_blob_t(rr0->buffer.get(), pts_size);
    rr0->_done = true;
    dew_error_t derr{};
    deserialize_points(rr0->buffer_info, handler->header, handler->data_info[0], derr);
    handler->read_request.push_back(std::move(rr0));

    emscripten::val salvage_attr = reply["salvageAttr"];
    if (!salvage_attr.isNull() && !salvage_attr.isUndefined())
    {
      auto rr1 = std::make_shared<read_request_t>();
      uint32_t attr_size = 0;
      rr1->buffer = copy_val_to_wasm(salvage_attr, attr_size);
      rr1->buffer_info = dew_blob_t(rr1->buffer.get(), attr_size);
      rr1->_done = true;
      handler->data_info[1] = rr1->buffer_info;
      handler->read_request.push_back(std::move(rr1));
    }
    // Mark complete so a defensive is_done() would agree; the reads are already resolved.
    handler->target_count = int(handler->read_request.size());
    handler->done = handler->target_count;

    impl->data_handler = std::move(handler);
  }

  out._impl_data = std::move(impl);
  return out;
}

void worker_node_data_loader_t::cancel(render::load_handle_t handle)
{
  auto it = _pending.find(handle);
  if (it == _pending.end())
    return;
  for (auto &r : it->second.reads)
    if (r)
      r->set_cancelled();
  // A worker reply may still land after cancel; drain_replies drops it (no pending entry). We do not try to
  // recall a job already posted to a worker -- it finishes and its result is discarded.
  _pending.erase(it);
}

} // namespace dew::converter

#endif // __EMSCRIPTEN__
