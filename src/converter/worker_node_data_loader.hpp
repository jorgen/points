/************************************************************************
** Points - point cloud management software.
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

// The WebGL decode loader: instead of decoding a node inline on the browser main thread (what
// native_node_data_loader does when compiled to wasm), it streams the COMPRESSED blob bytes off disk/network
// (storage_handler::read(raw=true), still on the main thread's Asyncify loop) and hands them to a pool of
// decode Web Workers over postMessage. The whole decompress -> deserialize -> decode_node CPU pipeline then
// runs off the main thread; the GPU-ready buffers come back as Transferable ArrayBuffers.
//
// This TU is wasm-only: the entire body is guarded by __EMSCRIPTEN__ so the converter library still compiles
// it to nothing on native builds (data_source_converter.cpp includes this header on all platforms).

#ifdef __EMSCRIPTEN__

#include "node_data_loader.hpp" // render::node_data_loader_t / loaded_node_data_t
#include "storage_handler.hpp"  // storage_handler_t, read_request_t

#include <emscripten/val.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace points::converter
{

// True iff the web app installed a decode-worker pool as globalThis.__pointsDecodePool. The data source only
// routes decode through a worker when this is true; otherwise it keeps the inline (main-thread) decode path.
bool decode_worker_pool_available();

class worker_node_data_loader_t final : public render::node_data_loader_t
{
public:
  explicit worker_node_data_loader_t(storage_handler_t &storage_handler);

  render::load_handle_t request_load(const void *request_data, uint32_t request_size) override;
  bool is_ready(render::load_handle_t handle) override;
  render::loaded_node_data_t get_data(render::load_handle_t handle) override;
  void cancel(render::load_handle_t handle) override;

private:
  enum class phase_t
  {
    reading, // the 4 compressed blob reads are in flight
    posted,  // handed to a worker, awaiting its reply
    ready    // reply landed (stored in `reply`)
  };

  struct pending_t
  {
    phase_t phase = phase_t::reading;
    std::array<std::shared_ptr<read_request_t>, 4> reads{};
    point_format_t format[4]{};
    tree_config_t tree_config{};
    bool want_salvage = false; // leaf: ask the worker to also return the raw points+attr blobs (virtual LOD)
    emscripten::val reply = emscripten::val::undefined();
  };

  // Pull every completed reply out of the JS pool and stamp it onto its pending entry. Cheap no-op when the
  // pool has nothing new; called from is_ready.
  void drain_replies();
  // Build the postMessage payload from a pending entry's completed reads and hand it to the pool.
  void post_to_worker(uint64_t id, pending_t &p);

  storage_handler_t &_storage_handler;
  emscripten::val _pool; // globalThis.__pointsDecodePool
  uint64_t _next_handle = 1;
  std::unordered_map<uint64_t, pending_t> _pending;
};

} // namespace points::converter

#endif // __EMSCRIPTEN__
