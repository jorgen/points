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

// tree_set_t's second wait shape: request(), the fire-and-forget one.
//
// load() is covered indirectly and thoroughly -- every access query goes through it, and
// access_query_tests would fail loudly if it broke. request() has no such cover: it is there for a
// renderer, which has no automated test, and it returns before doing anything, so a version that
// silently loaded NOTHING would look exactly like a version that worked. Hence these.
//
// What the two shapes must agree on is the important part: the same tree, installed the same way. So
// the last test loads the same id both ways and compares the structure.

#include <doctest/doctest.h>

#include "blob_reader.hpp"
#include "storage_handler.hpp"
#include "tree_set.hpp"

#include <vio/event_loop.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

using namespace dew::core;

namespace
{

// Writes a handful of tree blobs and a registry that points at them, then reads them back through a
// tree_set_t. Building the trees directly (rather than converting a point cloud) keeps this test
// about the tree set: a registry with N locations and nothing resident is exactly the state a reader
// wakes up in.
struct tree_set_fixture_t : vio::about_to_block_t
{
  static constexpr uint32_t k_tree_count = 4;

  tree_set_fixture_t()
    : pool(2)
    , index_written(loop_thread.event_loop(), vio::event_bind_t::bind(*this, &tree_set_fixture_t::on_index_written))
    , storage_error(loop_thread.event_loop(), vio::event_bind_t::bind(*this, &tree_set_fixture_t::on_storage_error))
    , storage("tree_set_test_cache_file", pool, attributes, perf, index_written, storage_error, error)
  {
    loop_thread.event_loop().add_about_to_block_listener(this);
    REQUIRE(storage.upgrade_to_write(true).code == 0);
  }

  void about_to_block() override {}
  void on_index_written() {}
  void on_storage_error(const dew_error_t &&) {}

  // A registry of k_tree_count trees, all written to storage, none resident.
  std::unique_ptr<uint8_t[]> build_registry(uint32_t &out_size)
  {
    tree_registry_t source;
    source.tree_config.scale = 0.001;
    source.tree_config.offset[0] = source.tree_config.offset[1] = source.tree_config.offset[2] = -1000.0;
    source.tree_config.node_point_limit = 1024;
    source.root = tree_id_t{0};
    source.data.resize(k_tree_count);
    source.locations.resize(k_tree_count);
    source.tree_id_initialized.assign(k_tree_count, 0);
    // Parallel to `locations` and asserted to match by tree_registry_serialize.
    source.tree_state.assign(k_tree_count, 0);
    source.tree_band.assign(k_tree_count, tree_band_none);

    std::vector<tree_id_t> ids;
    std::vector<serialized_tree_t> serialized;
    for (uint32_t i = 0; i < k_tree_count; i++)
    {
      auto tree = std::make_unique<tree_t>();
      tree->id = tree_id_t{i};
      tree->magnitude = 3;
      tree->morton_min = {};
      tree->morton_max = {};
      // Distinct per tree, so installing the wrong one is visible rather than harmless.
      tree->morton_max.data[0] = uint64_t(1) << (20 + i);
      // tree_serialize asserts nodes/skips/node_ids/data are the same length at every level, so a
      // node needs its point collection too -- a bare node entry is not a valid tree.
      tree->node_ids[0].push_back(0);
      tree->nodes[0].push_back(0);
      tree->skips[0].push_back(0);
      points_collection_t collection;
      collection.point_count = 0;
      collection.min = tree->morton_min;
      collection.max = tree->morton_max;
      collection.min_lod = morton::morton_lod(collection.min, collection.max);
      tree->data[0].push_back(std::move(collection));

      ids.push_back(tree_id_t{i});
      serialized.push_back(tree_serialize(*tree));
      source.data[i] = std::move(tree);
      source.tree_id_initialized[i] = 1;
    }

    // Through the real write path, so the blobs are compressed exactly as a converted dataset's are
    // and the read under test has to undo it.
    std::vector<storage_location_t> locations;
    {
      std::unique_lock<std::mutex> lock(mutex);
      done = false;
      storage.write_trees(std::move(ids), std::move(serialized), [this, &locations](std::vector<tree_id_t> &&, std::vector<storage_location_t> &&locs, dew_error_t &&write_error) {
        std::unique_lock<std::mutex> inner(mutex);
        REQUIRE(write_error.code == 0);
        locations = std::move(locs);
        done = true;
        cond.notify_all();
      });
      cond.wait(lock, [this] { return done; });
    }
    REQUIRE(locations.size() == k_tree_count);
    for (uint32_t i = 0; i < k_tree_count; i++)
      source.locations[i] = locations[i];

    auto serialized_registry = tree_registry_serialize(source);
    out_size = uint32_t(serialized_registry.size);
    auto copy = std::make_unique<uint8_t[]>(out_size);
    memcpy(copy.get(), serialized_registry.data.get(), out_size);
    return copy;
  }

  dew_error_t error;
  vio::thread_pool_t pool;
  vio::thread_with_event_loop_t loop_thread;
  attributes_configs_t attributes;
  perf_stats_t perf;
  vio::event_pipe_t<void> index_written;
  vio::event_pipe_t<dew_error_t> storage_error;
  dew::converter::storage_handler_t storage;

  std::mutex mutex;
  std::condition_variable cond;
  bool done = false;
};

// Run `fn` on the loop and wait for it. Everything tree_set_t does is loop-thread work, so a test
// that pokes it from outside would be testing something the real callers never do.
template <typename Fn> void on_loop(vio::event_loop_t &loop, Fn &&fn)
{
  std::promise<void> ran;
  auto future = ran.get_future();
  loop.run_in_loop([&fn, &ran]() {
    fn();
    ran.set_value();
  });
  future.wait();
}

bool wait_resident(tree_set_t &trees, tree_id_t id, int timeout_ms = 10000)
{
  for (int i = 0; i < timeout_ms && !trees.resident(id); i++)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return trees.resident(id);
}

} // namespace

TEST_CASE("tree_set: request() loads a tree with nobody awaiting it")
{
  // The whole point of the fire-and-forget shape: the caller returns immediately and the tree turns
  // up later. A renderer relies on exactly this -- it draws without the tree this frame and finds it
  // resident on a subsequent one.
  tree_set_fixture_t fixture;
  uint32_t registry_size = 0;
  auto registry_blob = fixture.build_registry(registry_size);

  tree_set_t trees(fixture.storage.reader(), fixture.loop_thread.event_loop());
  REQUIRE(trees.initialize(registry_blob, registry_size).code == 0);
  REQUIRE(!trees.resident(tree_id_t{0}));

  trees.request({tree_id_t{0}, tree_id_t{2}});

  REQUIRE(wait_resident(trees, tree_id_t{0}));
  REQUIRE(wait_resident(trees, tree_id_t{2}));
  // Only what was asked for. A request that loaded the whole registry would pass every other
  // assertion here while destroying the laziness the design exists for.
  REQUIRE(!trees.resident(tree_id_t{1}));
  REQUIRE(!trees.resident(tree_id_t{3}));

  trees.begin_shutdown();
}

TEST_CASE("tree_set: repeated requests for the same tree start only one load")
{
  // A renderer calls request() every frame with the walk's output, which repeats the same ids until
  // they land. Without the dedupe that is a fresh read per frame for the same blob -- invisible in
  // the rendered result, ruinous in IO.
  //
  // Asserted on loads_started rather than in_flight ON PURPOSE. in_flight is a race to observe: the
  // first load usually finishes before a later duplicate is even considered, at which point the
  // residency check masks a missing dedupe and the test passes while testing nothing. (It did,
  // briefly.) loads_started is monotonic, so it counts what the dedupe actually let through.
  tree_set_fixture_t fixture;
  uint32_t registry_size = 0;
  auto registry_blob = fixture.build_registry(registry_size);

  tree_set_t trees(fixture.storage.reader(), fixture.loop_thread.event_loop());
  REQUIRE(trees.initialize(registry_blob, registry_size).code == 0);

  // Repeats inside ONE batch: the first spawns and suspends at its read, so the tree is not resident
  // yet and only _requested can stop the other two.
  trees.request({tree_id_t{1}, tree_id_t{1}, tree_id_t{1}});
  REQUIRE(wait_resident(trees, tree_id_t{1}));
  REQUIRE(trees.loads_started() == 1);

  // And across batches, once it is resident, asking again is free rather than a re-read.
  trees.request({tree_id_t{1}});
  on_loop(fixture.loop_thread.event_loop(), [&] {});
  REQUIRE(trees.loads_started() == 1);
  REQUIRE(trees.in_flight() == 0);

  trees.begin_shutdown();
}

TEST_CASE("tree_set: begin_shutdown stops new loads")
{
  // Teardown ordering: the dataset stops requesting before the reader's loop is joined. If
  // begin_shutdown were advisory, a load could be issued against a reader that is going away.
  tree_set_fixture_t fixture;
  uint32_t registry_size = 0;
  auto registry_blob = fixture.build_registry(registry_size);

  tree_set_t trees(fixture.storage.reader(), fixture.loop_thread.event_loop());
  REQUIRE(trees.initialize(registry_blob, registry_size).code == 0);
  trees.begin_shutdown();

  uint32_t in_flight = 99;
  trees.request({tree_id_t{0}});
  on_loop(fixture.loop_thread.event_loop(), [&] { in_flight = trees.in_flight(); });
  REQUIRE(in_flight == 0);
  REQUIRE(!trees.resident(tree_id_t{0}));
}

TEST_CASE("tree_set: both wait shapes install the same tree")
{
  // The reason both live on one implementation. If request() and load() ever diverged on the install
  // -- a missed tree_compute_leaves_collapsed, a different residency store -- the renderer and the
  // query engine would disagree about the same dataset, and only one of them has tests.
  tree_set_fixture_t fixture;
  uint32_t registry_size = 0;
  auto registry_blob = fixture.build_registry(registry_size);

  tree_set_t via_request(fixture.storage.reader(), fixture.loop_thread.event_loop());
  REQUIRE(via_request.initialize(registry_blob, registry_size).code == 0);
  via_request.request({tree_id_t{3}});
  REQUIRE(wait_resident(via_request, tree_id_t{3}));

  tree_set_t via_load(fixture.storage.reader(), fixture.loop_thread.event_loop());
  REQUIRE(via_load.initialize(registry_blob, registry_size).code == 0);
  bool loaded = false;
  dew_error_t load_error;
  on_loop(fixture.loop_thread.event_loop(), [&] {
    [](tree_set_t *set, bool *out, dew_error_t *err) -> vio::detached_task_t { *out = co_await set->load(tree_id_t{3}, *err); }(&via_load, &loaded, &load_error);
  });
  REQUIRE(wait_resident(via_load, tree_id_t{3}));
  REQUIRE(loaded);
  REQUIRE(load_error.code == 0);

  const auto *a = via_request.registry().get(tree_id_t{3});
  const auto *b = via_load.registry().get(tree_id_t{3});
  REQUIRE(a);
  REQUIRE(b);
  REQUIRE(a->id.data == b->id.data);
  REQUIRE(a->magnitude == b->magnitude);
  REQUIRE(a->morton_max.data[0] == b->morton_max.data[0]);
  REQUIRE(a->leaves_collapsed == b->leaves_collapsed);

  via_request.begin_shutdown();
  via_load.begin_shutdown();
}
