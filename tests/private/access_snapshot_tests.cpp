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

// Characterization net for the dew_access extraction.
//
// These tests pin the READ-SIDE observable behaviour of the dataset format -- tree structure,
// serialization round-trips, storage-map addressing, attribute-index resolution and blob content --
// so that moving this code from src/converter into src/access can be proven to be a pure code move.
//
// They deliberately assert INVARIANTS rather than golden byte constants: a hard-coded hash would
// encode this compiler's struct padding and would have to be regenerated on every platform, which
// makes it worthless as a regression net. Instead each test states a property that a mechanical
// move cannot break unless it broke something real. Every snapshot value is also printed via
// MESSAGE() so a developer can eyeball a pre/post-move diff of the whole structure.
//
// When the code moves, only the namespace (dew::converter -> dew::access) and the include paths in
// this file should need to change. If anything else has to change, the move was not mechanical.

#include <doctest/doctest.h>
#include <fmt/printf.h>

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#include <vio/thread_pool.h>

#include "attributes_configs.hpp"
#include "dataset_types.hpp"
#include "input_header.hpp"
#include "storage_handler.hpp"
#include "tree.hpp"
#include "tree_build.hpp"

#include <dew/core/format.h>
#include <dew/converter/converter.h>
#include <dew/converter/default_attribute_names.h>

#include <map>
#include <string>
#include <vector>

using namespace dew::converter;

namespace
{

// FNV-1a over a byte range. Only used to compare two buffers produced in the SAME process (a
// round-trip), never against a checked-in constant -- see the header comment.
uint64_t hash_bytes(const void *data, size_t size)
{
  auto *bytes = static_cast<const uint8_t *>(data);
  uint64_t h = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < size; i++)
  {
    h ^= bytes[i];
    h *= 0x100000001b3ull;
  }
  return h;
}

tree_config_t make_tree_config()
{
  tree_config_t tree_config;
  tree_config.scale = 0.001;
  tree_config.offset[0] = 0.0;
  tree_config.offset[1] = 0.0;
  tree_config.offset[2] = 0.0;
  return tree_config;
}

struct write_result_t
{
  storage_header_t header;
  attributes_id_t attribute_id;
  std::vector<storage_location_t> locations;
};

// Minimal standalone read/write stack: a storage_handler_t plus the two registries it needs. This
// is the same shape tests/private/tree_tests.cpp uses, reduced to what a read-path snapshot needs.
struct snapshot_fixture_t : vio::about_to_block_t
{
  explicit snapshot_fixture_t(uint32_t node_limit = 256)
    : worker_thread_pool(4)
    , event_loop(event_loop_thread.event_loop())
    , tree_config(make_tree_config())
    , tree_registry(node_limit, tree_config)
    , index_written(event_loop, bind(&snapshot_fixture_t::handle_index_written))
    , storage_error(event_loop, bind(&snapshot_fixture_t::handle_storage_error))
    , storage_handler("access_snapshot_cache_file", worker_thread_pool, attributes_configs, perf_stats, index_written, storage_error, error)
  {
    event_loop.add_about_to_block_listener(this);
    REQUIRE(storage_handler.upgrade_to_write(true).code == 0);
  }

  void about_to_block() override {}
  void handle_index_written() {}
  void handle_storage_error(const dew_error_t &&e) { fmt::print("storage error: {}\n", e.msg); }

  // Write one synthetic chunk of morton-coded points with an intensity attribute, and block until
  // the storage handler reports its blob locations.
  write_result_t write_points(uint64_t morton_min, uint64_t morton_max, uint32_t point_count)
  {
    dew_converter_attributes_t attrs;
    dew_converter_attributes_add_attribute(&attrs, DEW_ATTRIBUTE_XYZ, uint32_t(strlen(DEW_ATTRIBUTE_XYZ)), dew_type_m64, dew_components_1);
    dew_converter_attributes_add_attribute(&attrs, DEW_ATTRIBUTE_INTENSITY, uint32_t(strlen(DEW_ATTRIBUTE_INTENSITY)), dew_type_u8, dew_components_1);
    auto attribute_id = attributes_configs.get_attribute_config_index(std::move(attrs));
    auto formats = attributes_configs.get_format_components(attribute_id);

    points_t points;
    points.header.input_id = {next_input_id++, 0};
    points.header.morton_min = {};
    points.header.morton_max = {};
    points.header.morton_min.data[0] = morton_min;
    points.header.morton_max.data[0] = morton_max;
    points.header.point_count = point_count;
    points.header.lod_span = morton::morton_lod(points.header.morton_min, points.header.morton_max);
    points.header.point_format = formats[0];
    attribute_buffers_initialize(formats, points.buffers, point_count);

    auto *morton_buffer = reinterpret_cast<morton::morton64_t *>(points.buffers.data[0].get());
    auto *intensity_buffer = points.buffers.data[1].get();
    const uint64_t step = std::max<uint64_t>((morton_max - morton_min) / std::max<uint32_t>(point_count - 1, 1), 1);
    uint64_t value = morton_min;
    for (uint32_t i = 0; i < point_count; i++)
    {
      morton_buffer[i].data[0] = value;
      intensity_buffer[i] = uint8_t(i);
      if (value + step < morton_max)
        value += step;
    }

    std::unique_lock<std::mutex> lock(write_mutex);
    write_done = false;
    storage_handler.write(points.header, attribute_id, std::move(points.buffers),
                          [this](const storage_header_t &header, attributes_id_t id, std::vector<storage_location_t> locations, const dew_error_t &) {
                            std::unique_lock<std::mutex> inner(write_mutex);
                            write_done = true;
                            last_write = {header, id, std::move(locations)};
                            write_cond.notify_all();
                          });
    write_cond.wait(lock, [this] { return write_done; });
    return last_write;
  }

  dew_error_t error;
  vio::thread_pool_t worker_thread_pool;
  vio::thread_with_event_loop_t event_loop_thread;
  vio::event_loop_t &event_loop;
  tree_config_t tree_config;
  tree_registry_t tree_registry;
  attributes_configs_t attributes_configs;
  vio::event_pipe_t<void> index_written;
  vio::event_pipe_t<dew_error_t> storage_error;
  perf_stats_t perf_stats;
  storage_handler_t storage_handler;

  uint32_t next_input_id = 0;
  std::mutex write_mutex;
  std::condition_variable write_cond;
  bool write_done = false;
  write_result_t last_write;
};

// Build a multi-level, multi-subtree octree.
//
// Subdivision here is driven by MORTON SPAN, not by point count: a chunk whose span is much
// narrower than the root's cell has to descend, and a chunk that bottoms out past level 4 forces a
// sub-tree hop. (Point-count-driven splitting only kicks in above tree_config.node_point_limit,
// which defaults to 200k -- far more points than a unit test wants to synthesize.) The spans below
// mirror tests/private/tree_tests.cpp's "add_new_subtree" case: 3*5*2 bits establishes a
// magnitude-2 root, and 3*5 bits then lands past level 4.
tree_id_t build_sample_tree(snapshot_fixture_t &fixture)
{
  constexpr uint64_t wide_span = (uint64_t(1) << (3 * 5 * 2)) - 1;
  constexpr uint64_t narrow_span = (uint64_t(1) << (3 * 5)) - 1;

  auto first = fixture.write_points(0, wide_span, 256);
  auto root_id = tree_initialize(fixture.tree_registry, fixture.storage_handler, first.header, first.attribute_id, std::move(first.locations));

  auto second = fixture.write_points(0, narrow_span, 256);
  root_id = tree_add_points(fixture.tree_registry, fixture.storage_handler, root_id, second.header, second.attribute_id, std::move(second.locations));

  // A third chunk offset into a different octant, so the tree has more than one populated branch
  // and the skips arrays carry a non-trivial layout.
  auto third = fixture.write_points(uint64_t(1) << 20, (uint64_t(1) << 20) + narrow_span, 256);
  root_id = tree_add_points(fixture.tree_registry, fixture.storage_handler, root_id, third.header, third.attribute_id, std::move(third.locations));

  return root_id;
}

// The full structural fingerprint of a registry: what a pure code move must preserve exactly.
struct registry_snapshot_t
{
  uint32_t tree_count = 0;
  uint32_t root_id = 0;
  double scale = 0.0;
  double offset[3] = {};
  // per tree: magnitude, then the node count at each of the five levels
  std::vector<std::array<uint32_t, 6>> per_tree_levels;
  // every (input_data_id, attribute_index) -> storage_location, flattened and ordered
  std::map<std::pair<uint64_t, int>, storage_location_t> storage_map;
  uint64_t total_subset_points = 0;
};

registry_snapshot_t snapshot_registry(const tree_registry_t &registry)
{
  registry_snapshot_t snap;
  snap.tree_count = registry.current_id;
  snap.root_id = registry.root.data;
  snap.scale = registry.tree_config.scale;
  for (int i = 0; i < 3; i++)
    snap.offset[i] = registry.tree_config.offset[i];

  for (uint32_t t = 0; t < registry.current_id; t++)
  {
    const auto *tree = registry.get(tree_id_t(t));
    if (!tree)
      continue;
    std::array<uint32_t, 6> levels{};
    levels[0] = tree->magnitude;
    for (int level = 0; level < 5; level++)
    {
      levels[size_t(level) + 1] = uint32_t(tree->nodes[level].size());
      for (const auto &collection : tree->data[level])
        for (const auto &subset : collection.data)
          snap.total_subset_points += subset.count.data;
    }
    snap.per_tree_levels.push_back(levels);

    tree->storage_map.for_each([&](input_data_id_t id, attributes_id_t, const std::vector<storage_location_t> &locations) {
      uint64_t key = (uint64_t(id.data) << 32) | id.sub;
      for (int i = 0; i < int(locations.size()); i++)
        snap.storage_map[{key, i}] = locations[size_t(i)];
    });
  }
  return snap;
}

void require_snapshots_equal(const registry_snapshot_t &a, const registry_snapshot_t &b)
{
  REQUIRE(a.tree_count == b.tree_count);
  REQUIRE(a.root_id == b.root_id);
  REQUIRE(a.scale == b.scale);
  for (int i = 0; i < 3; i++)
    REQUIRE(a.offset[i] == b.offset[i]);
  REQUIRE(a.total_subset_points == b.total_subset_points);
  REQUIRE(a.per_tree_levels.size() == b.per_tree_levels.size());
  for (size_t t = 0; t < a.per_tree_levels.size(); t++)
    for (size_t l = 0; l < a.per_tree_levels[t].size(); l++)
      REQUIRE(a.per_tree_levels[t][l] == b.per_tree_levels[t][l]);
  REQUIRE(a.storage_map.size() == b.storage_map.size());
  for (const auto &[key, location] : a.storage_map)
  {
    auto it = b.storage_map.find(key);
    REQUIRE(it != b.storage_map.end());
    REQUIRE(location == it->second);
  }
}

} // namespace

TEST_CASE("access snapshot: tree registry serialize round-trip is byte-stable")
{
  snapshot_fixture_t fixture;
  build_sample_tree(fixture);

  auto first_pass = tree_registry_serialize(fixture.tree_registry);
  REQUIRE(first_pass.size > 0);

  // A registry blob must deserialize into a registry that re-serializes to the identical bytes.
  // This is the strongest single guard on the format code: it covers tree_config versioning, the
  // chunk-ref table, the per-tree location vectors and the tree_state/tree_band parallel arrays.
  tree_registry_t restored;
  auto buffer = std::make_unique<uint8_t[]>(uint32_t(first_pass.size));
  memcpy(buffer.get(), first_pass.data.get(), size_t(first_pass.size));
  auto deserialize_error = tree_registry_deserialize(buffer, uint32_t(first_pass.size), restored);
  REQUIRE(deserialize_error.code == 0);

  auto second_pass = tree_registry_serialize(restored);
  REQUIRE(second_pass.size == first_pass.size);
  REQUIRE(hash_bytes(first_pass.data.get(), size_t(first_pass.size)) == hash_bytes(second_pass.data.get(), size_t(second_pass.size)));

  MESSAGE("registry blob size: " << first_pass.size << "  hash: " << hash_bytes(first_pass.data.get(), size_t(first_pass.size)));
  REQUIRE(restored.root.data == fixture.tree_registry.root.data);
  REQUIRE(restored.current_id == fixture.tree_registry.current_id);
  REQUIRE(restored.tree_config.scale == fixture.tree_registry.tree_config.scale);
  REQUIRE(restored.tree_config.node_point_limit == fixture.tree_registry.tree_config.node_point_limit);

  // The registry is fully sized at deserialize and the read path never grows it. The synchronous
  // node accessors in dew_access depend on exactly this invariant.
  REQUIRE(restored.data.size() == restored.locations.size());
  REQUIRE(restored.tree_id_initialized.size() >= restored.current_id);
}

TEST_CASE("access snapshot: per-tree serialize round-trip preserves structure")
{
  snapshot_fixture_t fixture;
  build_sample_tree(fixture);

  uint32_t trees_checked = 0;
  for (uint32_t t = 0; t < fixture.tree_registry.current_id; t++)
  {
    const auto *tree = fixture.tree_registry.get(tree_id_t(t));
    if (!tree)
      continue;

    auto first_pass = tree_serialize(*tree);
    REQUIRE(first_pass.size > 0);

    tree_t restored;
    dew_error_t tree_error;
    REQUIRE(tree_deserialize(first_pass, restored, tree_error));
    REQUIRE(tree_error.code == 0);

    auto second_pass = tree_serialize(restored);
    REQUIRE(second_pass.size == first_pass.size);
    REQUIRE(hash_bytes(first_pass.data.get(), size_t(first_pass.size)) == hash_bytes(second_pass.data.get(), size_t(second_pass.size)));

    REQUIRE(restored.id.data == tree->id.data);
    REQUIRE(restored.magnitude == tree->magnitude);
    REQUIRE(restored.morton_min == tree->morton_min);
    REQUIRE(restored.morton_max == tree->morton_max);
    REQUIRE(restored.sub_trees.size() == tree->sub_trees.size());
    for (int level = 0; level < 5; level++)
    {
      REQUIRE(restored.nodes[level] == tree->nodes[level]);
      REQUIRE(restored.skips[level] == tree->skips[level]);
      REQUIRE(restored.node_ids[level] == tree->node_ids[level]);
      REQUIRE(restored.data[level].size() == tree->data[level].size());
    }
    trees_checked++;
  }
  MESSAGE("trees round-tripped: " << trees_checked);
  REQUIRE(trees_checked > 0);
}

TEST_CASE("access snapshot: registry structure fingerprint survives a serialize round-trip")
{
  snapshot_fixture_t fixture;
  build_sample_tree(fixture);

  auto before = snapshot_registry(fixture.tree_registry);

  auto blob = tree_registry_serialize(fixture.tree_registry);
  auto buffer = std::make_unique<uint8_t[]>(uint32_t(blob.size));
  memcpy(buffer.get(), blob.data.get(), size_t(blob.size));
  tree_registry_t restored;
  REQUIRE(tree_registry_deserialize(buffer, uint32_t(blob.size), restored).code == 0);

  // The registry blob carries tree LOCATIONS, not the trees themselves, so re-populate the restored
  // registry's tree objects from their serialized forms before fingerprinting.
  for (uint32_t t = 0; t < fixture.tree_registry.current_id; t++)
  {
    const auto *tree = fixture.tree_registry.get(tree_id_t(t));
    if (!tree)
      continue;
    auto serialized = tree_serialize(*tree);
    auto restored_tree = std::make_unique<tree_t>();
    dew_error_t tree_error;
    REQUIRE(tree_deserialize(serialized, *restored_tree, tree_error));
    tree_compute_leaves_collapsed(*restored_tree, restored);
    restored.data[t] = std::move(restored_tree);
    restored.tree_id_initialized[t] = 1;
  }

  auto after = snapshot_registry(restored);
  require_snapshots_equal(before, after);

  MESSAGE("trees: " << before.tree_count << "  root: " << before.root_id
                    << "  storage entries: " << before.storage_map.size()
                    << "  subset points: " << before.total_subset_points);
  for (size_t t = 0; t < before.per_tree_levels.size(); t++)
    MESSAGE("  tree " << t << " magnitude " << before.per_tree_levels[t][0] << " levels "
                      << before.per_tree_levels[t][1] << "/" << before.per_tree_levels[t][2] << "/"
                      << before.per_tree_levels[t][3] << "/" << before.per_tree_levels[t][4] << "/"
                      << before.per_tree_levels[t][5]);
}

TEST_CASE("access snapshot: attribute index resolution and storage addressing")
{
  snapshot_fixture_t fixture;
  build_sample_tree(fixture);

  // Attribute name -> slot index is the lookup that turns a walker subset into a blob address, and
  // the index it returns is exactly the index into input_storage_map_t::location().
  attributes_id_t attribute_id{0};
  auto xyz = fixture.attributes_configs.get_attribute_index(attribute_id, DEW_ATTRIBUTE_XYZ);
  auto intensity = fixture.attributes_configs.get_attribute_index(attribute_id, DEW_ATTRIBUTE_INTENSITY);
  auto missing = fixture.attributes_configs.get_attribute_index(attribute_id, "no_such_attribute");

  REQUIRE(xyz.index == 0); // slot 0 is always the position/morton buffer
  REQUIRE(xyz.format.type == dew_type_m64);
  REQUIRE(intensity.index == 1);
  REQUIRE(intensity.format.type == dew_type_u8);
  REQUIRE(missing.index == -1); // a missing attribute reports -1, it does not throw or abort

  uint32_t located = 0;
  uint32_t empty_slots = 0;
  for (uint32_t t = 0; t < fixture.tree_registry.current_id; t++)
  {
    const auto *tree = fixture.tree_registry.get(tree_id_t(t));
    if (!tree)
      continue;
    tree->storage_map.for_each([&](input_data_id_t id, attributes_id_t attr_id, const std::vector<storage_location_t> &locations) {
      auto index = fixture.attributes_configs.get_attribute_index(attr_id, DEW_ATTRIBUTE_XYZ);
      REQUIRE(index.index >= 0);
      auto location = tree->storage_map.location(id, index.index);
      // size == 0 is the codebase-wide "absent slot" marker; offset == 0 is a VALID blob location
      // under object storage, so emptiness must never be tested on offset.
      if (location.size == 0)
        empty_slots++;
      else
      {
        REQUIRE(location == locations[size_t(index.index)]);
        located++;
      }
    });
  }
  MESSAGE("position blobs located: " << located << "  empty slots: " << empty_slots);
  REQUIRE(located > 0);
}

TEST_CASE("access snapshot: blob read-back yields the written points")
{
  snapshot_fixture_t fixture;
  build_sample_tree(fixture);

  // Read every position blob back through the storage handler and verify it deserializes into a
  // storage_header_t plus point data of the size the header claims. This exercises the read path
  // end to end: cache lookup, backend read, decompression, and deserialize_points.
  uint32_t blobs_read = 0;
  uint64_t total_points = 0;
  for (uint32_t t = 0; t < fixture.tree_registry.current_id; t++)
  {
    const auto *tree = fixture.tree_registry.get(tree_id_t(t));
    if (!tree)
      continue;
    std::vector<std::pair<input_data_id_t, attributes_id_t>> entries;
    tree->storage_map.for_each([&](input_data_id_t id, attributes_id_t attr_id, const std::vector<storage_location_t> &) { entries.emplace_back(id, attr_id); });

    for (auto &[id, attr_id] : entries)
    {
      auto index = fixture.attributes_configs.get_attribute_index(attr_id, DEW_ATTRIBUTE_XYZ);
      auto location = tree->storage_map.location(id, index.index);
      if (location.size == 0)
        continue;

      read_only_points_t read(fixture.storage_handler, location);
      REQUIRE(read.error.code == 0);
      REQUIRE(read.header.point_count > 0);
      // Slot 0 is a storage_header_t followed by the morton codes; the payload must match the
      // header's point count at the header's own point format.
      REQUIRE(read.data.size == read.header.point_count * uint32_t(size_for_format(read.header.point_format.type, read.header.point_format.components)));
      REQUIRE(read.header.morton_min <= read.header.morton_max);
      total_points += read.header.point_count;
      blobs_read++;
    }
  }
  MESSAGE("position blobs read: " << blobs_read << "  points: " << total_points);
  REQUIRE(blobs_read > 0);
  REQUIRE(total_points > 0);
}

TEST_CASE("access snapshot: LOD units are distinguishable from leaf units")
{
  // The single most dangerous property for a data-access API: interior LOD nodes hold SUBSAMPLED
  // COPIES of their descendants, so unioning subsets across levels double-counts. The only way to
  // tell them apart is the input_data_id_t bit encoding, so pin it here.
  input_data_id_t leaf{7, 0};
  input_data_id_t collapsed{7, 0x40000000u};
  input_data_id_t lod{7, 0x80000000u};

  REQUIRE(input_data_id_is_leaf(leaf));
  REQUIRE(!input_data_id_is_collapsed_leaf(leaf));

  // A collapsed leaf is still leaf DATA (bit 31 clear) -- it must not be filtered out of a
  // full-resolution query -- but it is disjoint from reader-chunk ids (bit 30 set).
  REQUIRE(input_data_id_is_leaf(collapsed));
  REQUIRE(input_data_id_is_collapsed_leaf(collapsed));

  // LOD units carry bit 31 and are what a full-resolution query must exclude.
  REQUIRE(!input_data_id_is_leaf(lod));
  REQUIRE(!input_data_id_is_collapsed_leaf(lod));
}
