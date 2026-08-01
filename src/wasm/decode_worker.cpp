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

// Pure-CPU decode Web Worker module (built + node-smoke-tested under EMSDK; browser E2E still wants a look).
//
// This is the WebGL counterpart of the native convert_pool: a lean wasm module, with NO WebGL, NO
// emscripten_fetch, and NO Asyncify, meant to run inside a Web Worker. The main render module streams the
// COMPRESSED blob bytes over the network (Asyncify/FETCH, on its own thread) and hands them here via
// postMessage; this module runs the whole CPU pipeline off the browser main thread:
//
//     compressed blob bytes  --decompress_any-->  raw bytes
//                            --deserialize_points-->  storage_header + points buffer
//                            --decode_node-->  GPU-ready vertex / attribute / rep_level (+ LOD prefix)
//
// then posts the GPU-ready buffers back as Transferable ArrayBuffers (zero-copy) for the main module to
// upload to WebGL. All three steps already exist and are storage/loop/GL-free; the decode_node() seam
// (node_decode.hpp) is what makes this composition possible without dragging the storage handler.
//
// The full wiring is in place: storage_handler::read(raw=true) hands the compressed bytes to
// worker_node_data_loader (src/converter), which posts them to a JS pool (decodeWorkerPool.ts) that feeds a
// pool of these workers. The result marshalling below returns OWNED Uint8Array copies (no cross-call
// lifetime), and the JS worker transfers them to the main thread. Node-smoke-tested; browser E2E still wants
// a look. See examples/renderer/web/src/decodeWorker.ts (worker side) and the integration notes at the bottom.

#include "../converter/compressor.hpp"                 // decompress_any, has_compression_magic
#include "../converter/node_decode.hpp"                // decode_node, decode_input_t
#include "../converter/point_buffer_render_helper.hpp" // decode_input_t
#include "../converter/storage_handler.hpp"            // deserialize_points  (TODO: extract to trim deps)

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <vector>

using namespace dew::converter;
using namespace dew::render;

namespace
{
// Copy a JS Uint8Array (compressed blob bytes) into a wasm-owned buffer.
std::shared_ptr<uint8_t[]> copy_in(const emscripten::val &u8, uint32_t &size_out)
{
  if (u8.isNull() || u8.isUndefined())
  {
    size_out = 0;
    return {};
  }
  const uint32_t n = u8["length"].as<uint32_t>();
  auto buf = std::make_shared<uint8_t[]>(n ? n : 1);
  // Copy JS -> wasm through a typed-array view over OUR destination buffer (created after the alloc, so it
  // reflects any memory-growth relocation): dst.set(u8). Do NOT go via Module.HEAPU8 -- MODULARIZE builds do
  // not export it, so val::module_property("HEAPU8") is undefined and .set() throws.
  emscripten::val dst(emscripten::typed_memory_view(n, buf.get()));
  dst.call<void>("set", u8);
  size_out = n;
  return buf;
}

// Decompress one blob slot if it carries the PCM magic; otherwise keep the raw bytes. Returns owned bytes.
std::shared_ptr<uint8_t[]> decompress_slot(const std::shared_ptr<uint8_t[]> &raw, uint32_t raw_size, uint32_t &out_size, dew_error_t &error)
{
  if (!raw || raw_size == 0)
  {
    out_size = 0;
    return {};
  }
  if (!has_compression_magic(raw.get(), raw_size))
  {
    out_size = raw_size;
    return raw;
  }
  auto r = decompress_any(raw.get(), raw_size);
  if (r.error.code != 0)
  {
    error = r.error;
    out_size = 0;
    return {};
  }
  out_size = r.size;
  return r.data;
}
} // namespace

// Decode one node. `msg` is a plain JS object (see decodeWorker.ts):
//   { treeScale:number, treeOffset:[x,y,z],
//     formats:[{type,components} x4],
//     buffers:[Uint8Array|null x4]   // COMPRESSED blob bytes, one per attribute slot }
// Returns a JS object with the GPU-ready buffers + metadata (the JS side transfers the ArrayBuffers back).
emscripten::val decode_node_js(emscripten::val msg)
{
  tree_config_t tree_config{};
  tree_config.scale = msg["treeScale"].as<double>();
  {
    emscripten::val off = msg["treeOffset"];
    tree_config.offset[0] = off[0].as<double>();
    tree_config.offset[1] = off[1].as<double>();
    tree_config.offset[2] = off[2].as<double>();
  }

  decode_input_t in;
  emscripten::val formats = msg["formats"];
  emscripten::val buffers = msg["buffers"];
  dew_error_t error{};

  for (int i = 0; i < 4; ++i)
  {
    in.point_format[i] = point_format_t(static_cast<dew_type_t>(formats[i]["type"].as<int>()),
                                        static_cast<dew_components_t>(formats[i]["components"].as<int>()));
    uint32_t raw_size = 0;
    auto raw = copy_in(buffers[i], raw_size);
    uint32_t dsize = 0;
    in.buffers[i] = decompress_slot(raw, raw_size, dsize, error);
    in.data_info[i] = dew_converter_buffer_t(in.buffers[i] ? in.buffers[i].get() : nullptr, dsize);
  }

  // A leaf that may be promoted to virtual subnodes needs its raw (decompressed, pre-reorder) points + attr
  // blobs shipped back so the render module can rebuild a salvage handler. Capture their full decompressed
  // sizes now, BEFORE deserialize_points shrinks data_info[0] to just the payload region.
  emscripten::val ws = msg["wantSalvage"];
  const bool want_salvage = !ws.isUndefined() && !ws.isNull() && ws.as<bool>();
  const uint32_t salvage0_size = in.data_info[0].size;
  const uint32_t salvage1_size = in.data_info[1].size;

  // Buffer 0 is the serialized points blob: pull the storage_header out and point data_info[0] at the points.
  if (in.buffers[0])
  {
    dew_error_t deser_error{};
    deserialize_points(in.data_info[0], in.header, in.data_info[0], deser_error);
    if (deser_error.code != 0)
      error = deser_error;
  }

  // The actual CPU decode -- identical to the native convert_pool path.
  dew::render::loaded_node_data_t out = decode_node(in, tree_config, /*salvage_handler=*/nullptr);

  // Marshal the result back to JS. Each buffer is copied into an OWNED Uint8Array (new Uint8Array(view) clones
  // per spec — its ArrayBuffer does not alias the wasm heap), so `out` can be freed immediately here and there
  // is no cross-call lifetime to manage. The worker then transfers those buffers to the main thread.
  auto owned_copy = [](const void *data, uint32_t size) -> emscripten::val {
    if (!data || size == 0)
      return emscripten::val::null();
    emscripten::val view(emscripten::typed_memory_view(size, static_cast<const uint8_t *>(data)));
    return emscripten::val::global("Uint8Array").new_(view); // clones out of the heap into its own buffer
  };

  emscripten::val result = emscripten::val::object();
  result.set("pointCount", out.point_count);
  result.set("hasLodOrder", out.has_lod_order);
  result.set("drawType", static_cast<int>(out.draw_type));
  result.set("vertexType", static_cast<int>(out.vertex_type));
  result.set("vertexComponents", static_cast<int>(out.vertex_components));
  result.set("attributeType", static_cast<int>(out.attribute_type));
  result.set("attributeComponents", static_cast<int>(out.attribute_components));
  {
    emscripten::val offset = emscripten::val::array();
    offset.set(0, out.offset[0]);
    offset.set(1, out.offset[1]);
    offset.set(2, out.offset[2]);
    result.set("offset", offset);
    emscripten::val prefix = emscripten::val::array();
    for (int i = 0; i < 64; ++i)
      prefix.set(i, out.prefix_count[i]);
    result.set("prefixCount", prefix);
  }
  result.set("vertex", owned_copy(out.vertex_data, out.vertex_data_size));
  result.set("attribute", owned_copy(out.attribute_data, out.attribute_data_size));
  result.set("repLevel", owned_copy(out.rep_level_data, out.rep_level_data_size));
  if (want_salvage)
  {
    // The FULL decompressed points blob (incl. its storage header) + attr blob, so the render module can
    // deserialize_points them back into a salvage dyn_points_data_handler_t for virtual subdivision.
    result.set("salvagePoints", owned_copy(in.buffers[0] ? in.buffers[0].get() : nullptr, salvage0_size));
    result.set("salvageAttr", owned_copy(in.buffers[1] ? in.buffers[1].get() : nullptr, salvage1_size));
  }
  // `out` (and its _impl_data buffers) is dropped here — the JS holds independent copies.
  return result;
}

EMSCRIPTEN_BINDINGS(dew_decode_worker)
{
  emscripten::function("decodeNode", &decode_node_js);
}

// ---------------------------------------------------------------------------------------------------------
// AS BUILT (main render module -> this worker)
//
// 1. Storage: storage_handler::read(location, raw=true) / do_read_request return the COMPRESSED bytes
//    unchanged on the decode-worker path (they skip the inline decompress). [DONE]
// 2. Loader: src/converter/worker_node_data_loader.{hpp,cpp} (wasm-only) issues the 4 raw reads, posts the
//    compressed buffers (+ formats + tree_config) to the JS pool, and reconstructs loaded_node_data_t from
//    the reply's transferred ArrayBuffers (_impl_data owns the copies). data_source_converter picks it over
//    the inline native loader when globalThis.__dewDecodePool exists. [DONE]
// 3. A pool of these workers (decodeWorkerPool.ts, ~hardwareConcurrency) parallelises decode. [DONE]
// 4. deserialize_points now lives in conversion_types.hpp (storage-free), so this worker links a lean source
//    set (node_decode + compressor + tree + morton) instead of the whole converter. [DONE]
//
// REMAINING: browser end-to-end verification (render a live S3 dataset with the pool installed) -- everything
// compiles under EMSDK and the worker module node-smoke-tests, but the on-screen result wants a human look.
// ---------------------------------------------------------------------------------------------------------
