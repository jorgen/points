/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2025  Jørgen Lind
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

// dew_copy: copy a dew point-cloud dataset between storage locations (packed .dew file, dir://, mem://,
// s3://, az://). The heavy point data (compressed blobs) is transferred VERBATIM -- never decoded or
// re-compressed -- and re-allocated in the destination's own layout, so it works across every format
// pair. Only the small index/trees are deserialized (to enumerate what to copy) and re-serialized (to
// carry the new blob locations). Sources may be packed files, legacy object datasets, or DEW2 buckets;
// object destinations are always written in the DEW2 layout (one object per blob + band + root
// manifest, see bucket_format.hpp). Credentials come from a per-side connection string (inline / @file / env:VAR) or
// the standard AWS_*/AZURE_* environment.

#include <dew/converter/connection_cli.h>

#include "bucket_format.hpp"
#include "conversion_types.hpp"
#include "error.hpp"
#include "input_storage_map.hpp"
#include "packed_file_backend.hpp"
#include "storage_backend.hpp"
#include "tree.hpp"
#include "url.hpp"

#include <vio/event_loop.h>
#include <vio/objstore/create_object_store.h>
#include <vio/task.h>

#include <fmt/printf.h>

#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace dew::converter;

namespace
{

struct copy_args_t
{
  std::string source_url;
  std::string dest_url;
  std::string source_connection_spec;
  std::string dest_connection_spec;
  bool force = false;
  bool quiet = false;
};

void print_usage(const char *prog)
{
  fmt::print(stderr,
             "Usage: {} [options] <source-url> <destination-url>\n"
             "\n"
             "Copy a dew point-cloud dataset between storage locations (a packed .dew file, dir://,\n"
             "mem://, s3://, az://). Compressed blobs are transferred verbatim (no re-encode) and\n"
             "re-laid-out for the destination format. Sources may be packed files, legacy object\n"
             "datasets or DEW2 buckets; object destinations are always written as DEW2 buckets.\n"
             "\n"
             "Options:\n"
             "  -s, --source-connection <spec>       connection string for the source store\n"
             "  -d, --destination-connection <spec>  connection string for the destination store\n"
             "  -f, --force                          overwrite the destination if it already exists\n"
             "  -q, --quiet                          only print errors\n"
             "  -h, --help                           show this help\n"
             "\n"
             "A <spec> is an inline connection string (key=value;...), '@path' to read it from a file,\n"
             "or 'env:NAME' to read it from an environment variable. Credentials also fall back to the\n"
             "standard AWS_*/AZURE_* environment variables.\n"
             "\n",
             prog);
  fmt::print(stderr,
             "Examples:\n"
             "  {0} cloud.dew s3://bucket/cloud -d 'endpoint=https://minio:9000;access_key_id=..;secret_access_key=..;path_style=true'\n"
             "  {0} s3://bucket/cloud dir:///data/cloud -s env:PROD_CONN\n",
             prog);
}

bool parse_args(int argc, char **argv, copy_args_t &args)
{
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i)
  {
    std::string a = argv[i];
    auto take_value = [&](std::string &out) -> bool {
      if (i + 1 >= argc)
      {
        fmt::print(stderr, "missing value for {}\n", a);
        return false;
      }
      out = argv[++i];
      return true;
    };
    if (a == "-s" || a == "--source-connection")
    {
      if (!take_value(args.source_connection_spec))
        return false;
    }
    else if (a == "-d" || a == "--destination-connection")
    {
      if (!take_value(args.dest_connection_spec))
        return false;
    }
    else if (a == "-f" || a == "--force")
      args.force = true;
    else if (a == "-q" || a == "--quiet")
      args.quiet = true;
    else if (a == "-h" || a == "--help")
    {
      print_usage(argv[0]);
      std::exit(0); // help is not an error
    }
    else if (!a.empty() && a[0] == '-')
    {
      fmt::print(stderr, "unknown option: {}\n", a);
      return false;
    }
    else
      positional.push_back(std::move(a));
  }
  if (positional.size() != 2)
  {
    print_usage(argv[0]);
    return false;
  }
  args.source_url = std::move(positional[0]);
  args.dest_url = std::move(positional[1]);
  return true;
}

// Drive a coroutine returning dew_error_t on `loop` and block the calling thread until it finishes.
// Mirrors object_backend's run_on_loop_blocking: the loop runs on its own thread, so this is called from
// main (never from the loop thread). state/factory are by-value coroutine params (a lambda's captures die
// after the first suspension).
struct sync_wait_state_t
{
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  dew_error_t result;
};

template <typename Factory>
vio::task_t<void> sync_wait_coro(std::shared_ptr<sync_wait_state_t> state, Factory factory)
{
  auto err = co_await factory();
  {
    std::unique_lock<std::mutex> lk(state->m);
    state->result = std::move(err);
    state->done = true;
  }
  state->cv.notify_one();
  co_return;
}

template <typename Factory>
dew_error_t run_on_loop_blocking(vio::event_loop_t &loop, Factory factory)
{
  auto state = std::make_shared<sync_wait_state_t>();
  loop.run_in_loop([state, factory = std::move(factory)]() mutable -> vio::task_t<void> { return sync_wait_coro(state, std::move(factory)); });
  std::unique_lock<std::mutex> lk(state->m);
  state->cv.wait(lk, [&] { return state->done; });
  return state->result;
}

// A blob is uniquely identified by (file_id, offset); `size` is its length, not part of identity.
using blob_key_t = std::pair<uint32_t, uint64_t>;

// Step 1 of every copy: read + deserialize each tree from the source; collect the unique data-blob
// locations they reference. std::map keeps the blob order deterministic.
vio::task_t<dew_error_t> collect_source(storage_backend_t *src, tree_registry_t *registry, std::vector<std::pair<uint32_t, std::shared_ptr<tree_t>>> &trees, std::map<blob_key_t, storage_location_t> &unique_blobs)
{
  for (uint32_t i = 0; i < registry->locations.size(); ++i)
  {
    const storage_location_t loc = registry->locations[i];
    if (loc.size == 0)
      continue;
    std::shared_ptr<uint8_t[]> buffer(new uint8_t[loc.size]);
    uint32_t bytes_read = 0;
    if (auto e = co_await src->read_blob(loc, buffer.get(), bytes_read); e.code != 0)
      co_return e;
    serialized_tree_t serialized{buffer, int(loc.size)};
    auto tree = std::make_shared<tree_t>();
    dew_error_t de{};
    if (!tree_deserialize(serialized, *tree, de))
      co_return(de.code != 0 ? de : dew_error_t{-1, "failed to deserialize tree"});
    tree->storage_map.for_each([&](input_data_id_t, attributes_id_t, const std::vector<storage_location_t> &storage) {
      for (const auto &s : storage)
        if (s.size != 0)
          unique_blobs.emplace(blob_key_t{s.file_id, s.offset}, s);
    });
    trees.emplace_back(i, std::move(tree));
  }
  co_return dew_error_t{};
}

// The copy itself, run on the event-loop thread. src/dst/load/registry outlive it (main blocks).
vio::task_t<dew_error_t> do_copy(storage_backend_t *src, storage_backend_t *dst, index_load_t *load, tree_registry_t *registry, uint64_t *blob_count, uint64_t *tree_count)
{
  // 1. Enumerate trees + the unique data blobs they reference.
  std::vector<std::pair<uint32_t, std::shared_ptr<tree_t>>> trees;
  std::map<blob_key_t, storage_location_t> unique_blobs;
  if (auto e = co_await collect_source(src, registry, trees, unique_blobs); e.code != 0)
    co_return e;

  // 2. Copy each unique data blob verbatim into the destination; remember old -> new location.
  std::map<blob_key_t, storage_location_t> remap;
  for (const auto &[key, src_loc] : unique_blobs)
  {
    std::shared_ptr<uint8_t[]> buffer(new uint8_t[src_loc.size]);
    uint32_t bytes_read = 0;
    if (auto e = co_await src->read_blob(src_loc, buffer.get(), bytes_read); e.code != 0)
      co_return e;
    storage_location_t new_loc;
    dst->allocate_blob(src_loc.size, storage_backend_t::blob_kind_t::data, new_loc);
    if (auto e = co_await dst->write_allocated(new_loc, buffer); e.code != 0)
      co_return e;
    remap[key] = new_loc;
    ++*blob_count;
  }

  // 3. Rewrite each tree's storage locations in place (preserving attributes_id + the serialized
  //    ref_counts -- a rebuild via add_storage would reset every ref_count to 1 and corrupt the copy on
  //    a later reopen-for-mutation), re-serialize + write, and update the registry.
  for (auto &[i, tree] : trees)
  {
    tree->storage_map.remap_storage([&](std::vector<storage_location_t> &storage) {
      for (auto &s : storage)
        if (s.size != 0) // empty slot preserved as-is
          s = remap.at(blob_key_t{s.file_id, s.offset});
    });

    auto serialized = tree_serialize(*tree);
    if (!serialized.data)
      co_return dew_error_t{-1, "failed to serialize tree"};
    storage_location_t tree_loc;
    dst->allocate_blob(uint32_t(serialized.size), storage_backend_t::blob_kind_t::metadata, tree_loc);
    if (auto e = co_await dst->write_allocated(tree_loc, serialized.data); e.code != 0)
      co_return e;
    registry->locations[i] = tree_loc;
    ++*tree_count;
  }

  // 4. Serialize + write the tree registry. The copy is not bound to any bucket (no dataset uuid /
  //    residency travels with it), so bucket-mirror states must not dangle: demote uploaded -> final
  //    and clear band assignments.
  for (size_t i = 0; i < registry->tree_state.size(); ++i)
  {
    if (registry->tree_state[i] == uint8_t(tree_state_t::uploaded))
      registry->tree_state[i] = uint8_t(tree_state_t::final);
    if (i < registry->tree_band.size())
      registry->tree_band[i] = tree_band_none;
  }
  auto serialized_registry = tree_registry_serialize(*registry);
  if (!serialized_registry.data)
    co_return dew_error_t{-1, "failed to serialize tree registry"};
  storage_location_t registry_loc;
  dst->allocate_blob(uint32_t(serialized_registry.size), storage_backend_t::blob_kind_t::metadata, registry_loc);
  if (auto e = co_await dst->write_allocated(registry_loc, serialized_registry.data); e.code != 0)
    co_return e;

  // 5. Checkpoint: hand the metadata payloads (copied verbatim) + the new registry location to the
  //    backend, which writes the manifest/superblock (and its own free-list / next-id) last.
  checkpoint_t checkpoint;
  checkpoint.tree_registry = registry_loc;
  checkpoint.attribute_configs = std::shared_ptr<uint8_t[]>(load->attribute_configs.release());
  checkpoint.attribute_configs_size = load->attribute_configs_size;
  checkpoint.stats = std::shared_ptr<uint8_t[]>(load->stats.release());
  checkpoint.stats_size = load->stats_size;
  checkpoint.perf = std::shared_ptr<uint8_t[]>(load->perf.release());
  checkpoint.perf_size = load->perf_size;
  co_return co_await dst->write_index(std::move(checkpoint));
}

static dew_error_t to_points_error(const vio::error_t &e)
{
  return dew_error_t{e.code != 0 ? e.code : -1, e.msg};
}

// PUT one data object data/{object_id:08x} whose content is exactly `bytes`.
vio::task_t<dew_error_t> dew2_put_object(vio::objstore::io_manager_t *io, uint32_t object_id, const uint8_t *bytes, uint32_t size, uint64_t *object_count)
{
  auto data = std::make_shared<uint8_t[]>(size);
  memcpy(data.get(), bytes, size);
  auto r = co_await io->write_object(bucket_data_object_name(object_id), std::move(data), size);
  if (!r.has_value())
    co_return to_points_error(r.error());
  ++*object_count;
  co_return dew_error_t{};
}

// HEAD the root manifest to decide whether an object destination already holds a dataset.
vio::task_t<dew_error_t> dew2_probe_manifest(vio::objstore::io_manager_t *io, bool *exists)
{
  auto r = co_await io->object_info(bucket_root_manifest_name());
  if (!r.has_value())
    co_return to_points_error(r.error());
  *exists = r->exists;
  co_return dew_error_t{};
}

// Copy into a DEW2 bucket: every blob becomes its own immutable object data/{id:08x} (verbatim
// bytes, storage_location_t = {object_id, 0, size} -- whole-object reads, no ranges), one band
// manifest covering everything, then the root manifest (complete=1) as the atomic commit point.
// Mirrors upload_handler_t's commit order (data objects < band < root); deterministic object-id
// assignment means a re-run overwrites its own orphans. A fresh uuid is minted by the caller -- a
// copy is a new dataset generation, resumable by no cache.
vio::task_t<dew_error_t> do_copy_dew2(storage_backend_t *src, vio::objstore::io_manager_t *io, index_load_t *load, tree_registry_t *registry, const uint8_t (&uuid)[16], uint64_t *blob_count, uint64_t *tree_count, uint64_t *object_count)
{
  // 1. Enumerate trees + the unique data blobs they reference.
  std::vector<std::pair<uint32_t, std::shared_ptr<tree_t>>> trees;
  std::map<blob_key_t, storage_location_t> unique_blobs;
  if (auto e = co_await collect_source(src, registry, trees, unique_blobs); e.code != 0)
    co_return e;

  uint32_t next_object_id = 0;
  auto put_bytes = [&](const uint8_t *bytes, uint32_t size, storage_location_t &out) -> vio::task_t<dew_error_t> {
    out.file_id = next_object_id++;
    out.offset = 0;
    out.size = size;
    co_return co_await dew2_put_object(io, out.file_id, bytes, size, object_count);
  };

  // 2. Data blobs, verbatim, one object each.
  std::map<blob_key_t, storage_location_t> remap;
  std::vector<uint8_t> blob_bytes;
  for (const auto &[key, src_loc] : unique_blobs)
  {
    blob_bytes.resize(src_loc.size);
    uint32_t bytes_read = 0;
    if (auto e = co_await src->read_blob(src_loc, blob_bytes.data(), bytes_read); e.code != 0)
      co_return e;
    storage_location_t bucket_location;
    if (auto e = co_await put_bytes(blob_bytes.data(), src_loc.size, bucket_location); e.code != 0)
      co_return e;
    remap[key] = bucket_location;
    ++*blob_count;
  }

  // 3. Trees: remap the storage maps to object locations (preserving ref_counts, as in do_copy),
  //    re-serialize, append. The registry mirrors the bucket state: everything uploaded, band 0.
  band_manifest_t band;
  for (auto &[i, tree] : trees)
  {
    tree->storage_map.remap_storage([&](std::vector<storage_location_t> &storage) {
      for (auto &s : storage)
        if (s.size != 0)
          s = remap.at(blob_key_t{s.file_id, s.offset});
    });
    auto serialized = tree_serialize(*tree);
    if (!serialized.data)
      co_return dew_error_t{-1, "failed to serialize tree"};
    storage_location_t tree_loc;
    if (auto e = co_await put_bytes(serialized.data.get(), uint32_t(serialized.size), tree_loc); e.code != 0)
      co_return e;
    registry->locations[i] = tree_loc;
    if (i < registry->tree_state.size())
      registry->tree_state[i] = uint8_t(tree_state_t::uploaded);
    if (i < registry->tree_band.size())
      registry->tree_band[i] = 0;
    band.trees.push_back(band_tree_entry_t{i, tree_loc});
    ++*tree_count;
  }

  // 4. Registry + the metadata payloads (verbatim), each as its own object. Unlike incremental uploads,
  //    a copy has the source's stats/perf at hand, so the bucket carries them too.
  auto serialized_registry = tree_registry_serialize(*registry);
  if (!serialized_registry.data)
    co_return dew_error_t{-1, "failed to serialize tree registry"};
  storage_location_t registry_loc;
  if (auto e = co_await put_bytes(serialized_registry.data.get(), uint32_t(serialized_registry.size), registry_loc); e.code != 0)
    co_return e;
  storage_location_t attributes_loc = {};
  storage_location_t stats_loc = {};
  storage_location_t perf_loc = {};
  if (load->attribute_configs_size)
    if (auto e = co_await put_bytes(load->attribute_configs.get(), load->attribute_configs_size, attributes_loc); e.code != 0)
      co_return e;
  if (load->stats_size)
    if (auto e = co_await put_bytes(load->stats.get(), load->stats_size, stats_loc); e.code != 0)
      co_return e;
  if (load->perf_size)
    if (auto e = co_await put_bytes(load->perf.get(), load->perf_size, perf_loc); e.code != 0)
      co_return e;

  // 5. Band manifest (band 0 covers the whole dataset; terminal watermark). The dedup table stays
  //    empty: it maps cache-file offsets for resumed uploads, and no cache references a copy.
  band.band_id = 0;
  memcpy(band.dataset_uuid, uuid, sizeof(band.dataset_uuid));
  band.watermark.data[0] = band.watermark.data[1] = band.watermark.data[2] = ~uint64_t(0);
  band.first_object_id = 0;
  band.next_object_id = next_object_id;
  if (load->attribute_configs_size)
    band.attributes_configs_snapshot.assign(load->attribute_configs.get(), load->attribute_configs.get() + load->attribute_configs_size);
  auto band_bytes = serialize_band_manifest(band);
  if (band_bytes.empty())
    co_return dew_error_t{-1, "failed to serialize band manifest"};
  {
    auto data = std::make_shared<uint8_t[]>(band_bytes.size());
    memcpy(data.get(), band_bytes.data(), band_bytes.size());
    auto r = co_await io->write_object(bucket_band_name(0), std::move(data), band_bytes.size());
    if (!r.has_value())
      co_return to_points_error(r.error());
  }

  // 6. Root manifest LAST -- the commit point.
  root_manifest_t root;
  memcpy(root.dataset_uuid, uuid, sizeof(root.dataset_uuid));
  root.complete = 1;
  root.band_count = 1;
  root.next_object_id = next_object_id;
  root.tree_registry = registry_loc;
  root.attribute_configs = attributes_loc;
  root.compression_stats = stats_loc;
  root.perf_stats = perf_loc;
  auto root_data = serialize_root_manifest(root);
  auto r = co_await io->write_object(bucket_root_manifest_name(), std::move(root_data), k_root_manifest_size);
  if (!r.has_value())
    co_return to_points_error(r.error());
  co_return dew_error_t{};
}

} // namespace

int main(int argc, char **argv)
{
  copy_args_t args;
  if (!parse_args(argc, argv, args))
    return 1;

  std::string source_connection, dest_connection, conn_error;
  if (!dew::converter::cli::resolve_connection_spec(args.source_connection_spec, source_connection, conn_error))
  {
    fmt::print(stderr, "source connection: {}\n", conn_error);
    return 1;
  }
  if (!dew::converter::cli::resolve_connection_spec(args.dest_connection_spec, dest_connection, conn_error))
  {
    fmt::print(stderr, "destination connection: {}\n", conn_error);
    return 1;
  }

  // The event loop runs on its own thread so the (blocking) object-backend index read + the copy
  // coroutine can be driven from main via run_on_loop_blocking. Declared first => destroyed last.
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  dew_error_t err{};
  auto src = create_storage_backend(args.source_url, source_connection, loop, err);
  if (!src || err.code != 0)
  {
    fmt::print(stderr, "cannot open source '{}': {}\n", args.source_url, err.msg);
    return 1;
  }
  if (!src->exists())
  {
    fmt::print(stderr, "source dataset does not exist: {}\n", args.source_url);
    return 1;
  }

  // Object-store destinations are written in the DEW2 layout (driving the io_manager directly with
  // pack/band/root ordering); only local packed files still go through the storage_backend interface.
  const auto parsed_dest = parse_url(args.dest_url);
  const bool dest_is_object = !parsed_dest.scheme.empty() && parsed_dest.scheme != "file";

  std::unique_ptr<storage_backend_t> dst;
  std::unique_ptr<vio::objstore::io_manager_t> dest_io;
  if (dest_is_object)
  {
    auto io = vio::objstore::create_io_manager(args.dest_url, dest_connection, loop);
    if (!io.has_value())
    {
      fmt::print(stderr, "cannot open destination '{}': {}\n", args.dest_url, io.error().msg);
      return 1;
    }
    dest_io = std::move(io.value());
    bool dest_exists = false;
    err = run_on_loop_blocking(loop, [io_ptr = dest_io.get(), exists = &dest_exists]() -> vio::task_t<dew_error_t> { return dew2_probe_manifest(io_ptr, exists); });
    if (err.code != 0)
    {
      fmt::print(stderr, "cannot probe destination '{}': {}\n", args.dest_url, err.msg);
      return 1;
    }
    if (dest_exists && !args.force)
    {
      fmt::print(stderr, "destination already exists (use --force to overwrite): {}\n", args.dest_url);
      return 1;
    }
    // --force replaces the manifests; superseded data objects from the previous dataset become orphan
    // objects (no list op yet), same contract as the legacy object truncate.
  }
  else
  {
    dst = create_storage_backend(args.dest_url, dest_connection, loop, err);
    if (!dst || err.code != 0)
    {
      fmt::print(stderr, "cannot open destination '{}': {}\n", args.dest_url, err.msg);
      return 1;
    }
    if (dst->exists() && !args.force)
    {
      fmt::print(stderr, "destination already exists (use --force to overwrite): {}\n", args.dest_url);
      return 1;
    }
    if (err = dst->open_for_write(true); err.code != 0)
    {
      fmt::print(stderr, "cannot open destination for writing: {}\n", err.msg);
      return 1;
    }
  }

  index_load_t load{};
  if (err = src->read_index(load); err.code != 0)
  {
    fmt::print(stderr, "cannot read source index: {}\n", err.msg);
    return 1;
  }

  // A capped cache may have evicted blob bytes to ITS destination bucket (residency table in the
  // superblock extras). dew_copy attaches no destination reads, so refuse up front with a pointer at
  // the complete source instead of failing on the first evicted blob mid-copy.
  if (src->is_packed_file())
  {
    auto *packed = static_cast<packed_file_backend_t *>(src.get());
    if (auto *residency = packed->residency())
    {
      uint64_t evicted = 0;
      residency->for_each([&](const blob_residency_entry_t &e) {
        if (!residency->has_local_bytes(e))
          evicted++;
      });
      if (evicted != 0)
      {
        fmt::print(stderr, "source cache is missing {} blobs locally (evicted/spilled to its destination bucket); copy from the destination bucket url instead\n", evicted);
        return 1;
      }
    }
  }

  tree_registry_t registry;
  if (err = tree_registry_deserialize(load.tree_registry, load.tree_registry_size, registry); err.code != 0)
  {
    fmt::print(stderr, "cannot deserialize tree registry: {}\n", err.msg);
    return 1;
  }

  uint64_t blob_count = 0;
  uint64_t tree_count = 0;
  uint64_t object_count = 0;
  storage_backend_t *src_ptr = src.get();
  dew_error_t copy_err;
  if (dest_is_object)
  {
    // A copy is a new dataset generation: mint a fresh uuid (no cache can resume onto it).
    uint8_t uuid[16];
    std::random_device rd;
    for (auto &b : uuid)
      b = uint8_t(rd());
    copy_err = run_on_loop_blocking(loop, [src_ptr, io_ptr = dest_io.get(), load_ptr = &load, registry_ptr = &registry, &uuid, blobs = &blob_count, trees = &tree_count, objects = &object_count]() -> vio::task_t<dew_error_t> {
      return do_copy_dew2(src_ptr, io_ptr, load_ptr, registry_ptr, uuid, blobs, trees, objects);
    });
  }
  else
  {
    storage_backend_t *dst_ptr = dst.get();
    copy_err = run_on_loop_blocking(loop, [src_ptr, dst_ptr, load_ptr = &load, registry_ptr = &registry, blobs = &blob_count, treees = &tree_count]() -> vio::task_t<dew_error_t> {
      return do_copy(src_ptr, dst_ptr, load_ptr, registry_ptr, blobs, treees);
    });
  }
  if (copy_err.code != 0)
  {
    fmt::print(stderr, "copy failed: {}\n", copy_err.msg);
    return 1;
  }

  if (!args.quiet)
  {
    if (dest_is_object)
      fmt::print("Copied {} data blobs + {} trees as {} objects: {} -> {} (DEW2)\n", blob_count, tree_count, object_count, args.source_url, args.dest_url);
    else
      fmt::print("Copied {} data blobs + {} trees: {} -> {}\n", blob_count, tree_count, args.source_url, args.dest_url);
  }
  return 0;
}
