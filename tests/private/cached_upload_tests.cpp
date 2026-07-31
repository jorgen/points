#include <doctest/doctest.h>

#include <points/converter/converter.h>
#include <points/converter/default_attribute_names.h>

#include <bucket_format.hpp>
#include <input_storage_map.hpp>
#include <storage_backend.hpp>
#include <tree.hpp>

#include <vio/event_loop.h>
#include <vio/task.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <map>
#include <string>
#include <thread>
#include <vector>

// End-to-end destination mode through the PUBLIC C API: synthetic input files convert into a local
// cache while finalized subtrees upload to a dir:// bucket in the JLP2 layout. Also covers resume
// (reopen without re-upload; re-added done inputs skipped) and the uuid-mismatch refusal.

namespace
{

// Synthetic "file": name encodes a coordinate base; every file is a line of points at
// x = base..base+count-1 (i32, scale applied by the converter), rising morton with the base.
struct synthetic_state_t
{
  int64_t base = 0;
  uint32_t total = 0;
  uint32_t produced = 0;
};

int64_t base_from_name(const char *filename, size_t size)
{
  // name format: synth_<base>
  std::string name(filename, size);
  return std::stoll(name.substr(name.find('_') + 1));
}

points_converter_file_pre_init_info_t synthetic_pre_init(const char *filename, size_t filename_size, struct points_error_t **)
{
  points_converter_file_pre_init_info_t info = {};
  info.found_aabb_min = 1;
  info.aabb_min[0] = double(base_from_name(filename, filename_size));
  info.aabb_min[1] = 0.0;
  info.aabb_min[2] = 0.0;
  info.approximate_point_count = 4096;
  info.approximate_point_size_bytes = 16;
  info.found_point_count = 1;
  info.input_file_size_bytes = 4096 * 16;
  return info;
}

void synthetic_init(const char *filename, size_t filename_size, struct points_converter_header_t *header, struct points_converter_attributes_t *attributes, void **user_ptr, struct points_error_t **)
{
  auto *state = new synthetic_state_t();
  state->base = base_from_name(filename, filename_size);
  state->total = 4096;
  header->point_count = state->total;
  header->offset[0] = header->offset[1] = header->offset[2] = 0.0;
  header->scale[0] = header->scale[1] = header->scale[2] = 0.001;
  header->min[0] = double(state->base) * 0.001;
  header->min[1] = 0.0;
  header->min[2] = 0.0;
  header->max[0] = double(state->base + state->total) * 0.001;
  header->max[1] = 0.001;
  header->max[2] = 0.001;
  points_converter_attributes_add_attribute(attributes, POINTS_ATTRIBUTE_XYZ, uint32_t(strlen(POINTS_ATTRIBUTE_XYZ)), points_type_i32, points_components_3);
  *user_ptr = state;
}

void synthetic_convert_data(void *user_ptr, const struct points_converter_header_t *, const struct points_converter_attribute_t *, uint32_t, uint32_t max_points_to_convert, struct points_converter_buffer_t *buffers, uint32_t buffers_size,
                            uint32_t *points_read, uint8_t *done, struct points_error_t **)
{
  auto *state = static_cast<synthetic_state_t *>(user_ptr);
  REQUIRE(buffers_size >= 1);
  auto *xyz = static_cast<int32_t *>(buffers[0].data);
  const uint32_t remaining = state->total - state->produced;
  const uint32_t count = remaining < max_points_to_convert ? remaining : max_points_to_convert;
  for (uint32_t i = 0; i < count; i++)
  {
    xyz[i * 3 + 0] = int32_t(state->base + state->produced + i);
    xyz[i * 3 + 1] = int32_t((state->produced + i) % 7);
    xyz[i * 3 + 2] = int32_t((state->produced + i) % 3);
  }
  state->produced += count;
  *points_read = count;
  *done = state->produced == state->total ? 1 : 0;
}

void synthetic_destroy(void *user_ptr)
{
  delete static_cast<synthetic_state_t *>(user_ptr);
}

points_converter_file_convert_callbacks_t synthetic_callbacks()
{
  points_converter_file_convert_callbacks_t callbacks = {};
  callbacks.pre_init = synthetic_pre_init;
  callbacks.init = synthetic_init;
  callbacks.convert_data = synthetic_convert_data;
  callbacks.destroy_user_ptr = synthetic_destroy;
  return callbacks;
}

void add_file(points_converter_t *converter, const std::string &name)
{
  points_converter_str_buffer buffer;
  buffer.data = name.c_str();
  buffer.size = uint32_t(name.size());
  points_converter_add_data_file(converter, &buffer, 1);
}

template <typename T>
struct task_result_type;
template <typename T>
struct task_result_type<vio::task_t<T>>
{
  using type = T;
};

// Run a backend/io coroutine on the loop thread and block the test thread for its result. The inner
// coroutine takes promise/factory as PARAMETERS (copied references into its frame); the test thread's
// locals outlive it because fut.get() blocks.
template <typename F>
static auto loop_op(vio::event_loop_t &loop, F &&f)
{
  using result_t = typename task_result_type<decltype(f())>::type;
  std::promise<result_t> promise;
  auto fut = promise.get_future();
  loop.run_in_loop([&]() -> vio::task_t<void> {
    return [](std::promise<result_t> &p, F &fn) -> vio::task_t<void> {
      p.set_value(co_await fn());
      co_return;
    }(promise, f);
  });
  return fut.get();
}

// Read one blob through a storage backend from the test thread.
static points_error_t backend_read(vio::event_loop_t &loop, points::converter::storage_backend_t *backend, points::converter::storage_location_t loc, std::vector<uint8_t> &out)
{
  out.resize(loc.size);
  uint32_t bytes_read = 0;
  auto err = loop_op(loop, [&]() { return backend->read_blob(loc, out.data(), bytes_read); });
  if (err.code == 0 && bytes_read != loc.size)
    return points_error_t{1, "short read"};
  return err;
}

// storage-map key: one storage unit per (input id, sub).
using unit_key_t = std::pair<uint32_t, uint32_t>;

static std::map<unit_key_t, std::vector<points::converter::storage_location_t>> unit_map(points::converter::tree_t &tree)
{
  std::map<unit_key_t, std::vector<points::converter::storage_location_t>> units;
  tree.storage_map.for_each([&](points::converter::input_data_id_t id, points::converter::attributes_id_t, const std::vector<points::converter::storage_location_t> &storage) { units[{id.data, id.sub}] = storage; });
  return units;
}

} // namespace

TEST_CASE("destination mode end-to-end via public API: convert, upload, resume, uuid guard")
{
  const char *cache_path = "test_dest_cache.jlp";
  const char *bucket_dir = "test_dest_bucket";
  std::remove(cache_path);
  std::filesystem::remove_all(bucket_dir);
  const std::string destination = std::string("dir://") + bucket_dir;

  uint32_t bands_seen = 0;
  points_converter_upload_callbacks_t upload_callbacks = {};
  upload_callbacks.band_committed = [](void *user_ptr, uint32_t, const uint64_t[3]) { (*static_cast<uint32_t *>(user_ptr))++; };

  // ---- Session 1: convert two files, everything uploads, destination completes. ----
  {
    points_error_t *error = nullptr;
    auto *converter = points_converter_create_with_destination(cache_path, strlen(cache_path), destination.c_str(), destination.size(), nullptr, 0, points_open_file_semantics_truncate, &error);
    REQUIRE(converter != nullptr);
    points_converter_set_file_converter_callbacks(converter, synthetic_callbacks());
    points_converter_set_upload_callbacks(converter, upload_callbacks, &bands_seen);

    add_file(converter, "synth_0");
    add_file(converter, "synth_100000");
    points_converter_wait_local_complete(converter);

    points_converter_wait_idle(converter); // also drains the upload backlog
    points_converter_upload_state_t state = {};
    REQUIRE(points_converter_get_upload_state(converter, &state));
    REQUIRE(state.destination_complete == 1);
    REQUIRE(state.upload_parked == 0);
    REQUIRE(state.bands_committed >= 1);
    REQUIRE(state.bytes_uploaded > 0);
    REQUIRE(bands_seen == state.bands_committed);
    REQUIRE(points_converter_status(converter) == points_conversion_status_completed);
    points_converter_destroy(converter);
  }

  // The bucket's root manifest is a complete JLP2 dataset (dir:// = one file per object).
  uint32_t bands_after_first = 0;
  {
    auto manifest_path = std::filesystem::path(bucket_dir) / points::converter::bucket_root_manifest_name();
    REQUIRE(std::filesystem::exists(manifest_path));
    FILE *f = fopen(manifest_path.string().c_str(), "rb");
    REQUIRE(f);
    std::vector<uint8_t> bytes(points::converter::k_root_manifest_size);
    REQUIRE(fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
    fclose(f);
    points::converter::root_manifest_t root;
    REQUIRE(points::converter::deserialize_root_manifest(bytes.data(), uint32_t(bytes.size()), root).code == 0);
    REQUIRE(root.complete == 1);
    REQUIRE(root.tree_registry.size > 0);
    bands_after_first = root.band_count;
    REQUIRE(bands_after_first >= 1);
  }

  // ---- Session 2 (resume): same cache + destination, re-add the same inputs -> skipped, no new
  // bands, destination stays complete. ----
  {
    points_error_t *error = nullptr;
    auto *converter = points_converter_create_with_destination(cache_path, strlen(cache_path), destination.c_str(), destination.size(), nullptr, 0, points_open_file_semantics_open_existing, &error);
    REQUIRE(converter != nullptr);
    points_converter_set_file_converter_callbacks(converter, synthetic_callbacks());
    add_file(converter, "synth_0");
    add_file(converter, "synth_100000");
    points_converter_wait_idle(converter);
    points_converter_upload_state_t state = {};
    REQUIRE(points_converter_get_upload_state(converter, &state));
    REQUIRE(state.destination_complete == 1);
    REQUIRE(state.bands_committed == bands_after_first); // nothing re-uploaded
    points_converter_destroy(converter);
  }

  // ---- A DIFFERENT cache generation must be refused by the bucket (uuid mismatch). ----
  {
    const char *other_cache = "test_dest_cache_other.jlp";
    std::remove(other_cache);
    points_error_t *error = nullptr;
    auto *converter = points_converter_create_with_destination(other_cache, strlen(other_cache), destination.c_str(), destination.size(), nullptr, 0, points_open_file_semantics_truncate, &error);
    REQUIRE(converter == nullptr);
    REQUIRE(error != nullptr);
    points_error_destroy(error);
    std::remove(other_cache);
  }

  std::remove(cache_path);
  std::filesystem::remove_all(bucket_dir);
}

TEST_CASE("JLP2 bucket reads back through the object backend (renderer/tool read path)")
{
  namespace pc = points::converter;
  const char *cache_path = "test_jlp2_read_cache.jlp";
  const char *bucket_dir = "test_jlp2_read_bucket";
  std::remove(cache_path);
  std::filesystem::remove_all(bucket_dir);
  const std::string bucket_url = std::string("dir://") + bucket_dir;

  // Produce a complete JLP2 bucket.
  {
    points_error_t *error = nullptr;
    auto *converter = points_converter_create_with_destination(cache_path, strlen(cache_path), bucket_url.c_str(), bucket_url.size(), nullptr, 0, points_open_file_semantics_truncate, &error);
    REQUIRE(converter != nullptr);
    points_converter_set_file_converter_callbacks(converter, synthetic_callbacks());
    add_file(converter, "synth_0");
    add_file(converter, "synth_100000");
    points_converter_wait_idle(converter);
    points_converter_upload_state_t state = {};
    REQUIRE(points_converter_get_upload_state(converter, &state));
    REQUIRE(state.destination_complete == 1);
    points_converter_destroy(converter);
  }

  // The public API opens the bucket url directly (create fails if the sniff / registry load fails).
  {
    points_error_t *error = nullptr;
    auto *reader = points_converter_create(bucket_url.c_str(), bucket_url.size(), points_open_file_semantics_read_only, &error);
    REQUIRE(reader != nullptr);
    points_converter_destroy(reader);
  }

  // Deep verification: walk both datasets through their storage backends and byte-compare every
  // storage unit -- the bucket's ranged pack GETs must return exactly the cache's raw blobs.
  {
    vio::thread_with_event_loop_t loop_thread;
    auto &loop = loop_thread.event_loop();
    points_error_t err{};

    auto cache = pc::create_storage_backend(cache_path, loop, err);
    REQUIRE(cache);
    REQUIRE(err.code == 0);
    auto bucket = pc::create_storage_backend(bucket_url, loop, err);
    REQUIRE(bucket);
    REQUIRE(err.code == 0);
    REQUIRE(bucket->exists());

    pc::index_load_t cache_load{};
    REQUIRE(cache->read_index(cache_load).code == 0);
    pc::index_load_t bucket_load{};
    REQUIRE(bucket->read_index(bucket_load).code == 0);
    REQUIRE(bucket_load.attribute_configs_size == cache_load.attribute_configs_size);

    pc::tree_registry_t cache_registry;
    REQUIRE(pc::tree_registry_deserialize(cache_load.tree_registry, cache_load.tree_registry_size, cache_registry).code == 0);
    pc::tree_registry_t bucket_registry;
    REQUIRE(pc::tree_registry_deserialize(bucket_load.tree_registry, bucket_load.tree_registry_size, bucket_registry).code == 0);
    REQUIRE(bucket_registry.locations.size() == cache_registry.locations.size());

    uint32_t units_compared = 0;
    for (uint32_t i = 0; i < uint32_t(cache_registry.locations.size()); i++)
    {
      REQUIRE((bucket_registry.locations[i].size == 0) == (cache_registry.locations[i].size == 0));
      if (cache_registry.locations[i].size == 0)
        continue;
      REQUIRE(bucket_registry.tree_state[i] == uint8_t(pc::tree_state_t::uploaded));

      std::vector<uint8_t> cache_tree_bytes;
      REQUIRE(backend_read(loop, cache.get(), cache_registry.locations[i], cache_tree_bytes).code == 0);
      std::vector<uint8_t> bucket_tree_bytes;
      REQUIRE(backend_read(loop, bucket.get(), bucket_registry.locations[i], bucket_tree_bytes).code == 0);

      auto deserialize_tree = [](std::vector<uint8_t> &bytes, pc::tree_t &tree) {
        std::shared_ptr<uint8_t[]> buffer(new uint8_t[bytes.size()]);
        memcpy(buffer.get(), bytes.data(), bytes.size());
        pc::serialized_tree_t serialized{buffer, int(bytes.size())};
        points_error_t de{};
        REQUIRE(pc::tree_deserialize(serialized, tree, de));
      };
      pc::tree_t cache_tree, bucket_tree;
      deserialize_tree(cache_tree_bytes, cache_tree);
      deserialize_tree(bucket_tree_bytes, bucket_tree);

      auto cache_units = unit_map(cache_tree);
      auto bucket_units = unit_map(bucket_tree);
      REQUIRE(bucket_units.size() == cache_units.size());
      for (auto &[key, cache_locs] : cache_units)
      {
        auto it = bucket_units.find(key);
        REQUIRE(it != bucket_units.end());
        REQUIRE(it->second.size() == cache_locs.size());
        for (size_t b = 0; b < cache_locs.size(); b++)
        {
          REQUIRE(it->second[b].size == cache_locs[b].size);
          if (cache_locs[b].size == 0)
            continue;
          std::vector<uint8_t> cache_bytes, bucket_bytes;
          REQUIRE(backend_read(loop, cache.get(), cache_locs[b], cache_bytes).code == 0);
          REQUIRE(backend_read(loop, bucket.get(), it->second[b], bucket_bytes).code == 0);
          REQUIRE(cache_bytes == bucket_bytes);
          units_compared++;
        }
      }
    }
    REQUIRE(units_compared > 0);

    // A JLP2 bucket is read-only through the storage-backend interface.
    REQUIRE(bucket->open_for_write(false).code != 0);
  }

  // An incomplete bucket (upload still in flight / parked) is refused with a clear error.
  {
    auto manifest_path = std::filesystem::path(bucket_dir) / pc::bucket_root_manifest_name();
    std::vector<uint8_t> bytes(pc::k_root_manifest_size);
    FILE *f = fopen(manifest_path.string().c_str(), "rb");
    REQUIRE(f);
    REQUIRE(fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
    fclose(f);
    pc::root_manifest_t root;
    REQUIRE(pc::deserialize_root_manifest(bytes.data(), uint32_t(bytes.size()), root).code == 0);
    root.complete = 0;
    auto incomplete = pc::serialize_root_manifest(root);
    f = fopen(manifest_path.string().c_str(), "wb");
    REQUIRE(f);
    REQUIRE(fwrite(incomplete.get(), 1, pc::k_root_manifest_size, f) == pc::k_root_manifest_size);
    fclose(f);

    points_error_t *error = nullptr;
    auto *reader = points_converter_create(bucket_url.c_str(), bucket_url.size(), points_open_file_semantics_read_only, &error);
    REQUIRE(reader == nullptr);
    REQUIRE(error != nullptr);
    points_error_destroy(error);
  }

  std::remove(cache_path);
  std::filesystem::remove_all(bucket_dir);
}

TEST_CASE("synthetic callbacks local-only sanity")
{
  const char *cache_path = "test_synth_local.jlp";
  std::remove(cache_path);
  points_error_t *error = nullptr;
  auto *converter = points_converter_create(cache_path, strlen(cache_path), points_open_file_semantics_truncate, &error);
  REQUIRE(converter != nullptr);
  points_converter_set_file_converter_callbacks(converter, synthetic_callbacks());
  points_converter_runtime_callbacks_t runtime = {};
  runtime.error = [](void *, const points_error_t *err) {
    int code = 0; const char *str = nullptr; size_t len = 0;
    points_error_get_info(err, &code, &str, &len);
    fprintf(stderr, "[runtime error] %d: %.*s\n", code, int(len), str ? str : "");
    fflush(stderr);
  };
  runtime.warning = [](void *, const char *msg) { fprintf(stderr, "[warning] %s\n", msg); fflush(stderr); };
  runtime.done = [](void *) { fprintf(stderr, "[done]\n"); fflush(stderr); };
  points_converter_set_runtime_callbacks(converter, runtime, nullptr);
  add_file(converter, "synth_0");
  points_converter_wait_idle(converter);
  REQUIRE(points_converter_status(converter) == points_conversion_status_completed);
  points_converter_destroy(converter);
  std::remove(cache_path);
}
