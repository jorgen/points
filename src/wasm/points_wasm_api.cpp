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
#include <object_backend.hpp>
#include <storage_backend.hpp>

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

static void points_close(int handle)
{
  g_datasets.erase(handle);
}

EMSCRIPTEN_BINDINGS(points_data)
{
  emscripten::function("open", &points_open);
  emscripten::function("readIndex", &points_read_index);
  emscripten::function("readBlob", &points_read_blob);
  emscripten::function("exists", &points_exists);
  emscripten::function("lastError", &points_last_error);
  emscripten::function("close", &points_close);
}
