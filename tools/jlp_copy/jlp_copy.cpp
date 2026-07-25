/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2025  Jørgen Lind
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

// jlp_copy: copy a jlp point-cloud dataset between storage locations (packed .jlp file, dir://, mem://,
// s3://, az://). The heavy point data (compressed blobs) is transferred VERBATIM -- never decoded or
// re-compressed -- and re-allocated in the destination's own layout, so it works across every format
// pair. Only the small index/trees are deserialized (to enumerate what to copy) and re-serialized (to
// carry the new blob locations). Credentials come from a per-side connection string (inline / @file /
// env:VAR) or the standard AWS_*/AZURE_* environment.

#include <points/converter/connection_cli.h>

#include "conversion_types.hpp"
#include "error.hpp"
#include "input_storage_map.hpp"
#include "storage_backend.hpp"
#include "tree.hpp"

#include <vio/event_loop.h>
#include <vio/task.h>

#include <fmt/printf.h>

#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace points::converter;

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
             "Copy a jlp point-cloud dataset between storage locations (a packed .jlp file, dir://,\n"
             "mem://, s3://, az://). Compressed blobs are transferred verbatim (no re-encode) and\n"
             "re-laid-out for the destination format.\n"
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
             "  {0} cloud.jlp s3://bucket/cloud -d 'endpoint=https://minio:9000;access_key_id=..;secret_access_key=..;path_style=true'\n"
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

// Drive a coroutine returning points_error_t on `loop` and block the calling thread until it finishes.
// Mirrors object_backend's run_on_loop_blocking: the loop runs on its own thread, so this is called from
// main (never from the loop thread). state/factory are by-value coroutine params (a lambda's captures die
// after the first suspension).
struct sync_wait_state_t
{
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  points_error_t result;
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
points_error_t run_on_loop_blocking(vio::event_loop_t &loop, Factory factory)
{
  auto state = std::make_shared<sync_wait_state_t>();
  loop.run_in_loop([state, factory = std::move(factory)]() mutable -> vio::task_t<void> { return sync_wait_coro(state, std::move(factory)); });
  std::unique_lock<std::mutex> lk(state->m);
  state->cv.wait(lk, [&] { return state->done; });
  return state->result;
}

// A blob is uniquely identified by (file_id, offset); `size` is its length, not part of identity.
using blob_key_t = std::pair<uint32_t, uint64_t>;

// The copy itself, run on the event-loop thread. src/dst/load/registry outlive it (main blocks).
vio::task_t<points_error_t> do_copy(storage_backend_t *src, storage_backend_t *dst, index_load_t *load, tree_registry_t *registry, uint64_t *blob_count, uint64_t *tree_count)
{
  // 1. Read + deserialize each tree; collect the unique data-blob locations they reference.
  std::vector<std::pair<uint32_t, std::shared_ptr<tree_t>>> trees;
  std::map<blob_key_t, storage_location_t> unique_blobs;
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
    points_error_t de{};
    if (!tree_deserialize(serialized, *tree, de))
      co_return(de.code != 0 ? de : points_error_t{-1, "failed to deserialize tree"});
    tree->storage_map.for_each([&](input_data_id_t, attributes_id_t, const std::vector<storage_location_t> &storage) {
      for (const auto &s : storage)
        if (s.size != 0)
          unique_blobs.emplace(blob_key_t{s.file_id, s.offset}, s);
    });
    trees.emplace_back(i, std::move(tree));
  }

  // 2. Copy each unique data blob verbatim into the destination; remember old -> new location.
  std::map<blob_key_t, storage_location_t> remap;
  for (const auto &[key, src_loc] : unique_blobs)
  {
    std::shared_ptr<uint8_t[]> buffer(new uint8_t[src_loc.size]);
    uint32_t bytes_read = 0;
    if (auto e = co_await src->read_blob(src_loc, buffer.get(), bytes_read); e.code != 0)
      co_return e;
    storage_location_t new_loc;
    dst->allocate_blob(src_loc.size, new_loc);
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
      co_return points_error_t{-1, "failed to serialize tree"};
    storage_location_t tree_loc;
    dst->allocate_blob(uint32_t(serialized.size), tree_loc);
    if (auto e = co_await dst->write_allocated(tree_loc, serialized.data); e.code != 0)
      co_return e;
    registry->locations[i] = tree_loc;
    ++*tree_count;
  }

  // 4. Serialize + write the tree registry.
  auto serialized_registry = tree_registry_serialize(*registry);
  if (!serialized_registry.data)
    co_return points_error_t{-1, "failed to serialize tree registry"};
  storage_location_t registry_loc;
  dst->allocate_blob(uint32_t(serialized_registry.size), registry_loc);
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

} // namespace

int main(int argc, char **argv)
{
  copy_args_t args;
  if (!parse_args(argc, argv, args))
    return 1;

  std::string source_connection, dest_connection, conn_error;
  if (!points::converter::cli::resolve_connection_spec(args.source_connection_spec, source_connection, conn_error))
  {
    fmt::print(stderr, "source connection: {}\n", conn_error);
    return 1;
  }
  if (!points::converter::cli::resolve_connection_spec(args.dest_connection_spec, dest_connection, conn_error))
  {
    fmt::print(stderr, "destination connection: {}\n", conn_error);
    return 1;
  }

  // For plain AWS s3:// URLs with no credentials given, fall back to the AWS CLI's provider chain (~/.aws,
  // SSO, `aws login`, assume-role, ...), just like `aws s3` would.
  points::converter::cli::apply_aws_cli_credentials(args.source_url, source_connection);
  points::converter::cli::apply_aws_cli_credentials(args.dest_url, dest_connection);

  // The event loop runs on its own thread so the (blocking) object-backend index read + the copy
  // coroutine can be driven from main via run_on_loop_blocking. Declared first => destroyed last.
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  points_error_t err{};
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

  auto dst = create_storage_backend(args.dest_url, dest_connection, loop, err);
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

  index_load_t load{};
  if (err = src->read_index(load); err.code != 0)
  {
    fmt::print(stderr, "cannot read source index: {}\n", err.msg);
    return 1;
  }

  tree_registry_t registry;
  if (err = tree_registry_deserialize(load.tree_registry, load.tree_registry_size, registry); err.code != 0)
  {
    fmt::print(stderr, "cannot deserialize tree registry: {}\n", err.msg);
    return 1;
  }

  uint64_t blob_count = 0;
  uint64_t tree_count = 0;
  storage_backend_t *src_ptr = src.get();
  storage_backend_t *dst_ptr = dst.get();
  points_error_t copy_err = run_on_loop_blocking(loop, [src_ptr, dst_ptr, load_ptr = &load, registry_ptr = &registry, blobs = &blob_count, treees = &tree_count]() -> vio::task_t<points_error_t> {
    return do_copy(src_ptr, dst_ptr, load_ptr, registry_ptr, blobs, treees);
  });
  if (copy_err.code != 0)
  {
    fmt::print(stderr, "copy failed: {}\n", copy_err.msg);
    return 1;
  }

  if (!args.quiet)
    fmt::print("Copied {} data blobs + {} trees: {} -> {}\n", blob_count, tree_count, args.source_url, args.dest_url);
  return 0;
}
