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

// WebAssembly library entry point (embind). Exposes the points object-store READ path to JavaScript:
// obtain S3 credentials from the caller, load data from S3 over emscripten_fetch, and return DECOMPRESSED
// buffers. Built with -sASYNCIFY, so the synchronous-looking functions below (which drive the single
// cooperative event loop via a busy-yield) are seen from JS as async functions returning Promises.

#include <compressor.hpp>
#include <frustum_tree_walker.hpp>
#include <object_backend.hpp>
#include <point_buffer_render_helper.hpp>
#include <storage_backend.hpp>
#include <tree.hpp>

#include <vio/objstore/create_object_store.h>
#include <vio/platform/wasm/cooperative_runtime.h>

#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace points::converter;
using emscripten::val;

namespace
{
// One process-wide cooperative event loop; every dataset's IO runs on it.
vio::event_loop_t g_loop;
std::string g_last_error;

struct dataset_t
{
  std::unique_ptr<object_backend_t> backend;
  index_load_t index;
  tree_registry_t tree_registry; // deserialized on open; carries tree_config (scale) + node/tree locations
  bool tree_registry_ok = false;
};
std::map<int, dataset_t> g_datasets;
int g_next_handle = 1;

struct drive_state_t
{
  bool done = false;
  points_error_t result;
};

// The coroutine that actually drives the io. state and factory are BY-VALUE parameters so they are
// copied into the coroutine frame -- a coroutine *lambda*'s captures instead live in its closure, which
// is destroyed after the first suspension (use-after-free on resume). Same pattern as object_backend's
// sync_wait_coro.
template <typename Factory>
vio::task_t<void> drive_coro(std::shared_ptr<drive_state_t> state, Factory factory)
{
  state->result = co_await factory();
  state->done = true;
  co_return;
}

// Drive a task_t<points_error_t> to completion on g_loop, single-threaded: pump the loop and yield to
// the browser (so pending emscripten_fetch/XHR callbacks fire and post their coroutine resumes) until it
// finishes. Requires -sASYNCIFY. Mirrors object_backend's bootstrap wait.
template <typename Factory>
points_error_t drive_blocking(Factory factory)
{
  auto state = std::make_shared<drive_state_t>();
  // The lambda handed to run_in_loop is NOT a coroutine: it just forwards into drive_coro, whose
  // by-value parameters own copies of state/factory for the lifetime of the actual io.
  g_loop.run_in_loop([state, factory = std::move(factory)]() mutable -> vio::task_t<void> { return drive_coro(state, std::move(factory)); });
  while (!state->done)
  {
    g_loop.poll();
    emscripten_sleep(0);
  }
  return state->result;
}

std::string get_str(const val &o, const char *key, const char *dflt = "")
{
  val v = o[key];
  if (v.isUndefined() || v.isNull())
    return dflt;
  return v.as<std::string>();
}

bool get_bool(const val &o, const char *key, bool dflt)
{
  val v = o[key];
  if (v.isUndefined() || v.isNull())
    return dflt;
  return v.as<bool>();
}

int get_int(const val &o, const char *key, int dflt)
{
  val v = o[key];
  if (v.isUndefined() || v.isNull())
    return dflt;
  return v.as<int>();
}

// Copy wasm memory into a fresh JS-owned Uint8Array (the source buffer may be freed after this returns).
val to_uint8array(const uint8_t *data, uint32_t size)
{
  val u8 = val::global("Uint8Array").new_(size);
  if (size > 0)
    u8.call<void>("set", val(emscripten::typed_memory_view(size, data)));
  return u8;
}

// Read a blob by storage location (busy-yield) and, if it carries a compression header, decompress it;
// otherwise return the raw bytes. false + g_last_error on failure.
bool read_blob_decompressed(object_backend_t *backend, storage_location_t loc, std::shared_ptr<uint8_t[]> &out, uint32_t &out_size)
{
  auto raw = std::make_shared<std::vector<uint8_t>>(loc.size);
  auto br = std::make_shared<uint32_t>(0);
  points_error_t err = drive_blocking([backend, loc, raw, br]() { return backend->read_blob(loc, raw->data(), *br); });
  if (err.code != 0)
  {
    g_last_error = err.msg.empty() ? "read_blob failed" : err.msg;
    return false;
  }
  if (has_compression_magic(raw->data(), *br))
  {
    compression_result_t res = decompress_any(raw->data(), *br);
    if (res.error.code != 0)
    {
      g_last_error = res.error.msg.empty() ? "decompress failed" : res.error.msg;
      return false;
    }
    out = res.data;
    out_size = res.size;
  }
  else
  {
    out = std::make_shared<uint8_t[]>(*br);
    memcpy(out.get(), raw->data(), *br);
    out_size = *br;
  }
  return true;
}
} // namespace

// Open an S3 dataset. `config` is a JS object:
//   { accessKeyId, secretAccessKey, sessionToken?, region?, host, port?, bucket, prefix?,
//     pathStyle?=true, https?=false }
// Returns an integer handle (>= 1), or -1 on error (see pointsLastError). Drives the bootstrap
// (manifest probe + index read) via the busy-yield, so from JS this is async (returns a Promise).
static int points_open(val config)
{
  g_last_error.clear();
  vio::objstore::s3_io_manager_t::config_t cfg;
  cfg.https = get_bool(config, "https", false);
  cfg.host = get_str(config, "host", "127.0.0.1");
  cfg.port = uint16_t(get_int(config, "port", 0));
  cfg.region = get_str(config, "region", "us-east-1");
  cfg.bucket = get_str(config, "bucket");
  cfg.prefix = get_str(config, "prefix");
  cfg.access_key = get_str(config, "accessKeyId");
  cfg.secret_key = get_str(config, "secretAccessKey");
  cfg.session_token = get_str(config, "sessionToken");
  cfg.path_style = get_bool(config, "pathStyle", true);

  if (cfg.bucket.empty())
  {
    g_last_error = "config.bucket is required";
    return -1;
  }

  auto io = vio::objstore::create_s3_with_config(cfg, g_loop);
  dataset_t ds;
  ds.backend = std::make_unique<object_backend_t>(std::move(io), g_loop); // ctor busy-yields (HEAD manifest)
  points_error_t err = ds.backend->read_index(ds.index);                   // busy-yields (GET manifest + metadata)
  if (err.code != 0)
  {
    g_last_error = err.msg.empty() ? "read_index failed" : err.msg;
    return -1;
  }

  // Deserialize the tree registry (tree_config scale + per-tree/node storage locations) so readNode can
  // locate nodes. Best-effort: a dataset with no tree registry (e.g. metadata-only) still opens.
  if (ds.index.tree_registry_size > 0)
  {
    std::unique_ptr<uint8_t[]> tr;
    uint32_t tr_size = 0;
    if (has_compression_magic(ds.index.tree_registry.get(), ds.index.tree_registry_size))
    {
      compression_result_t res = decompress_any(ds.index.tree_registry.get(), ds.index.tree_registry_size);
      if (res.error.code == 0)
      {
        tr = std::make_unique<uint8_t[]>(res.size);
        memcpy(tr.get(), res.data.get(), res.size);
        tr_size = res.size;
      }
    }
    else
    {
      tr = std::make_unique<uint8_t[]>(ds.index.tree_registry_size);
      memcpy(tr.get(), ds.index.tree_registry.get(), ds.index.tree_registry_size);
      tr_size = ds.index.tree_registry_size;
    }
    if (tr_size > 0)
      ds.tree_registry_ok = tree_registry_deserialize(tr, tr_size, ds.tree_registry).code == 0;
  }

  int handle = g_next_handle++;
  g_datasets.emplace(handle, std::move(ds));
  return handle;
}

static std::string points_last_error()
{
  return g_last_error;
}

static bool points_exists(int handle)
{
  auto it = g_datasets.find(handle);
  return it != g_datasets.end() && it->second.backend->exists();
}

// The dataset index / metadata read on open. Returns a JS object of Uint8Arrays:
//   { attributeConfigs, treeRegistry, stats, perf }. treeRegistry holds the serialized tree structure
// from which node blob locations are derived (parsed by higher-level APIs / the caller).
static val points_read_index(int handle)
{
  g_last_error.clear();
  auto it = g_datasets.find(handle);
  if (it == g_datasets.end())
  {
    g_last_error = "invalid handle";
    return val::null();
  }
  const index_load_t &ix = it->second.index;
  val out = val::object();
  out.set("attributeConfigs", to_uint8array(ix.attribute_configs.get(), ix.attribute_configs_size));
  out.set("treeRegistry", to_uint8array(ix.tree_registry.get(), ix.tree_registry_size));
  out.set("stats", to_uint8array(ix.stats.get(), ix.stats_size));
  out.set("perf", to_uint8array(ix.perf.get(), ix.perf_size));
  return out;
}

// Read one blob by its storage location and return its DECOMPRESSED bytes as a Uint8Array (raw blobs,
// which carry no compression header, are returned as-is). Async from JS (busy-yields the fetch).
static val points_read_blob(int handle, double file_id, double offset, double size)
{
  g_last_error.clear();
  auto it = g_datasets.find(handle);
  if (it == g_datasets.end())
  {
    g_last_error = "invalid handle";
    return val::null();
  }
  storage_location_t loc;
  loc.file_id = uint32_t(file_id);
  loc.offset = uint64_t(offset);
  loc.size = uint32_t(size);

  auto raw = std::make_shared<std::vector<uint8_t>>(loc.size);
  auto bytes_read = std::make_shared<uint32_t>(0);
  object_backend_t *backend = it->second.backend.get();
  points_error_t err = drive_blocking([backend, loc, raw, bytes_read]() { return backend->read_blob(loc, raw->data(), *bytes_read); });
  if (err.code != 0)
  {
    g_last_error = err.msg.empty() ? "read_blob failed" : err.msg;
    return val::null();
  }

  if (has_compression_magic(raw->data(), *bytes_read))
  {
    compression_result_t res = decompress_any(raw->data(), *bytes_read);
    if (res.error.code != 0)
    {
      g_last_error = res.error.msg.empty() ? "decompress failed" : res.error.msg;
      return val::null();
    }
    return to_uint8array(res.data.get(), res.size);
  }
  return to_uint8array(raw->data(), *bytes_read);
}

// Read + decode the first point node of the root tree, end to end: tree deserialize -> node storage map
// -> read + decompress the positions blob -> morton decode. Returns { vertex: Float32Array (xyz, node-
// local), offset: [x,y,z] (node origin in world units), pointCount }, or null on error. This exercises
// the full render read path in wasm. Async from JS (busy-yields the fetches).
static val points_read_node(int handle)
{
  g_last_error.clear();
  auto it = g_datasets.find(handle);
  if (it == g_datasets.end())
  {
    g_last_error = "invalid handle";
    return val::null();
  }
  dataset_t &ds = it->second;
  if (!ds.tree_registry_ok)
  {
    g_last_error = "tree registry not loaded";
    return val::null();
  }
  object_backend_t *backend = ds.backend.get();

  // Root tree blob -> deserialize -> its storage map holds the node point locations.
  tree_id_t root = ds.tree_registry.root;
  if (root.data >= ds.tree_registry.locations.size())
  {
    g_last_error = "root tree location out of range";
    return val::null();
  }
  std::shared_ptr<uint8_t[]> tree_buf;
  uint32_t tree_size = 0;
  if (!read_blob_decompressed(backend, ds.tree_registry.locations[root.data], tree_buf, tree_size))
    return val::null();
  serialized_tree_t serialized{tree_buf, int(tree_size)};
  tree_t tree;
  points_error_t terr;
  if (!tree_deserialize(serialized, tree, terr))
  {
    g_last_error = terr.msg.empty() ? "tree_deserialize failed" : terr.msg;
    return val::null();
  }

  // First storage-map entry with a positions blob (attribute index 0).
  storage_location_t pos_loc{};
  bool found = false;
  tree.storage_map.for_each([&](input_data_id_t, attributes_id_t, const std::vector<storage_location_t> &storage) {
    if (!found && !storage.empty() && storage[0].size > 0)
    {
      pos_loc = storage[0];
      found = true;
    }
  });
  if (!found)
  {
    g_last_error = "root tree has no node with a positions blob";
    return val::null();
  }

  // Read + decompress the positions blob -> storage_header_t + morton point data.
  std::shared_ptr<uint8_t[]> pos_buf;
  uint32_t pos_size = 0;
  if (!read_blob_decompressed(backend, pos_loc, pos_buf, pos_size))
    return val::null();
  storage_header_t header{};
  points_converter_buffer_t point_data{};
  points_error_t derr;
  if (!deserialize_points(points_converter_buffer_t(pos_buf.get(), pos_size), header, point_data, derr))
  {
    g_last_error = derr.msg.empty() ? "deserialize_points failed" : derr.msg;
    return val::null();
  }

  // Morton -> float32 xyz via the render helper (hand-populate the data handler).
  point_format_t formats[4] = {header.point_format, point_format_t(), point_format_t(), point_format_t()};
  dyn_points_data_handler_t dh(formats);
  dh.header = header;
  dh.read_request.push_back(std::make_shared<read_request_t>());
  dh.read_request[0]->buffer = pos_buf;
  dh.read_request[0]->buffer_info = points_converter_buffer_t(pos_buf.get(), pos_size);
  dh.data_info[0] = point_data;
  dh.target_count = 1;
  dh.done = 1;

  dyn_points_draw_buffer_t draw;
  convert_points_to_vertex_data(ds.tree_registry.tree_config, dh.as_decode_input(), draw);

  uint32_t point_count = header.point_count;
  const float *xyz = reinterpret_cast<const float *>(draw.data[0].get());
  val out = val::object();
  out.set("pointCount", point_count);
  val offset = val::array();
  offset.call<void>("push", draw.offset[0]);
  offset.call<void>("push", draw.offset[1]);
  offset.call<void>("push", draw.offset[2]);
  out.set("offset", offset);
  val vertex = val::global("Float32Array").new_(point_count * 3);
  if (point_count > 0)
    vertex.call<void>("set", val(emscripten::typed_memory_view(size_t(point_count) * 3, xyz)));
  out.set("vertex", vertex);
  return out;
}

static void points_close(int handle)
{
  g_datasets.erase(handle);
}

EMSCRIPTEN_BINDINGS(points_data)
{
  emscripten::function("open", &points_open);
  emscripten::function("readIndex", &points_read_index);
  emscripten::function("readBlob", &points_read_blob);
  emscripten::function("readNode", &points_read_node);
  emscripten::function("exists", &points_exists);
  emscripten::function("lastError", &points_last_error);
  emscripten::function("close", &points_close);
}
