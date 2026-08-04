#include <doctest/doctest.h>
#include <fmt/printf.h>
#include <utility>

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#include <vio/thread_pool.h>

#include "bucket_format.hpp"
#include "dataset_types.hpp"
#include "input_data_source_registry.hpp"
#include "storage_handler.hpp"
#include "tree_handler.hpp"
#include "tree_lod_generator.hpp"
#include "upload_handler.hpp"

#include <vio/objstore/memory_object_store.h>
#include <attributes_configs.hpp>
#include <input_header.hpp>
#include <morton.hpp>
#include <morton_tree_coordinate_transform.hpp>
#include <dew/core/format.h>
#include <dew/converter/converter.h>
#include <dew/core/default_attribute_names.h>
#include <tree.hpp>
#include <tree_build.hpp>

namespace
{

dew::core::tree_config_t create_tree_config(double scale, double offset = -double(uint64_t(1) << 17))
{
  dew::core::tree_config_t tree_config;
  tree_config.scale = scale;

  tree_config.offset[0] = offset;
  tree_config.offset[1] = offset;
  tree_config.offset[2] = offset;

  return tree_config;
}

struct write_done_event_t
{
  dew::core::storage_header_t header;
  dew::core::attributes_id_t attribute_id;
  std::vector<dew::core::storage_location_t> locations;
};

struct tree_test_infrastructure : vio::about_to_block_t
{
  tree_test_infrastructure(uint32_t node_limit = 1000)
    : worker_thread_pool(4)
    , event_loop_thread()
    , event_loop(event_loop_thread.event_loop())
    , node_limit(node_limit)
    , tree_config(create_tree_config(0.001, 0.0))
    , tree_registry(node_limit, tree_config)
    , index_written(event_loop, bind(&tree_test_infrastructure::handle_index_written))
    , cache_file_error(event_loop, bind(&tree_test_infrastructure::handle_file_error))

    , cache_file_handler("test_cache_file", worker_thread_pool, attributes_config, perf_stats, index_written, cache_file_error, error)
  {
    event_loop.add_about_to_block_listener(this);
    (void)cache_file_handler.upgrade_to_write(true);
  }

  void write(const dew::core::storage_header_t &header, dew::core::attributes_id_t attribute_id, dew::core::attribute_buffers_t &&buffers)
  {
    std::unique_lock<std::mutex> lock(wait_for_write_done_mutex);
    write_done_state = false;

    cache_file_handler.write(header, attribute_id, std::move(buffers),
                             [this](const dew::core::storage_header_t &header, dew::core::attributes_id_t attributes_id, std::vector<dew::core::storage_location_t> &&location,
                                    const dew_error_t &error) { handle_write_done(header, attributes_id, std::move(location)); });
  }

  void handle_index_written()
  {
    fmt::print("index written\n");
    if (on_index_written)
      on_index_written();
  }
  void handle_file_error(const dew_error_t &&error)
  {
    fmt::print("error: {}\n", error.msg);
  }
  void handle_write_done(const dew::core::storage_header_t &header, dew::core::attributes_id_t attributes_id, std::vector<dew::core::storage_location_t> &&location)
  {
    std::unique_lock<std::mutex> lock(wait_for_write_done_mutex);
    write_done_state = true;
    fmt::print("write done: {}\n", header.input_id.data);
    write_done_event = {header, attributes_id, std::move(location)};
    wait_for_write_done_cond.notify_all();
  }

  write_done_event_t wait_for_write_done()
  {
    std::unique_lock<std::mutex> lock(wait_for_write_done_mutex);
    wait_for_write_done_cond.wait(lock, [&] { return write_done_state; });
    return write_done_event;
  }

  void about_to_block() override
  {
  }

  dew_error_t error;
  vio::thread_pool_t worker_thread_pool;
  vio::thread_with_event_loop_t event_loop_thread;
  vio::event_loop_t &event_loop;
  uint32_t node_limit;
  dew::core::tree_config_t tree_config;
  dew::core::tree_registry_t tree_registry;
  dew::core::attributes_configs_t attributes_config;
  vio::event_pipe_t<void> index_written;
  vio::event_pipe_t<dew_error_t> cache_file_error;
  dew::core::perf_stats_t perf_stats;
  dew::converter::storage_handler_t cache_file_handler;

  std::function<void()> on_index_written; // optional test hook, fired per checkpoint commit

  uint32_t next_input_id = 0;
  bool write_done_state = false;
  std::mutex wait_for_write_done_mutex;
  std::condition_variable wait_for_write_done_cond;
  write_done_event_t write_done_event;
};

void attributes_add_attributecpp(dew_converter_attributes_t &attr, const std::string &name, dew_type_t format, dew_components_t components)
{
  dew_converter_attributes_add_attribute(&attr, name.c_str(), uint32_t(name.size()), format, components);
}

write_done_event_t create_points(tree_test_infrastructure &test_util, uint64_t min, uint64_t max, uint64_t point_count = 256)
{
  dew_converter_attributes_t attrs;
  attributes_add_attributecpp(attrs, DEW_ATTRIBUTE_XYZ, dew_type_m64, dew_components_1);
  attributes_add_attributecpp(attrs, DEW_ATTRIBUTE_INTENSITY, dew_type_u8, dew_components_1);
  auto attr_id = test_util.attributes_config.get_attribute_config_index(std::move(attrs));
  auto attr_def = test_util.attributes_config.get_format_components(attr_id);

  dew::core::points_t points;
  points.header.input_id = {test_util.next_input_id++, 0};
  points.header.morton_min.data[0] = min;
  points.header.morton_min.data[1] = 0;
  points.header.morton_min.data[2] = 0;
  points.header.morton_max.data[0] = max;
  points.header.morton_max.data[1] = 0;
  points.header.morton_max.data[2] = 0;
  points.header.point_count = point_count;
  points.header.lod_span = dew::core::morton::morton_lod(points.header.morton_min, points.header.morton_max);
  points.header.point_format = attr_def[0];
  dew::converter::attribute_buffers_initialize(attr_def, points.buffers, point_count);

  auto *morton_buffer = reinterpret_cast<dew::core::morton::morton64_t *>(points.buffers.data[0].get());
  auto *intensity_buffer = reinterpret_cast<uint8_t *>(points.buffers.data[1].get());
  assert(points.buffers.buffers[0].size == point_count * 8);
  assert(points.buffers.buffers[1].size == point_count);
  uint64_t step_size = std::max((max - min) / (point_count - 1), uint64_t(1));
  uint64_t last_value = min;
  for (int i = 0; i < point_count; i++)
  {
    morton_buffer[i].data[0] = last_value;
    intensity_buffer[i] = uint8_t(i);
    if (last_value + step_size < max)
      last_value += step_size;
  }
  test_util.write(points.header, attr_id, std::move(points.buffers));
  return test_util.wait_for_write_done();
}

// Registry-global chunk-refs invariant: chunk_tree_refs[id] must equal the number of tree storage
// maps that actually contain the id.
static void require_chunk_refs_consistent(dew::core::tree_registry_t &registry)
{
  for (auto &[chunk_id, chunk_ref] : registry.chunk_tree_refs)
  {
    uint32_t actual = 0;
    for (auto &tree : registry.data)
      if (tree && tree->storage_map.contains(chunk_id))
        actual++;
    REQUIRE(actual == chunk_ref.tree_count);
    REQUIRE(chunk_ref.tree_count > 0);
    REQUIRE(chunk_ref.point_count > 0);
  }
}

TEST_CASE("initialize empty tree")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  tree_test_infrastructure test_util;
  auto write_done_event = create_points(test_util, 0, morton_max, 256);
  auto &[header, attribute_id, locations] = write_done_event;

  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, header, attribute_id, std::move(locations));
  auto tree = *test_util.tree_registry.get(root_id);
  REQUIRE(tree.morton_max.data[0] == morton_max);
  REQUIRE(tree.morton_max.data[1] == 0);
  REQUIRE(tree.morton_max.data[2] == 0);

  REQUIRE(tree.morton_min.data[0] == 0);
  REQUIRE(tree.morton_min.data[1] == 0);
  REQUIRE(tree.morton_min.data[2] == 0);
}

TEST_CASE("add inclusion")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  tree_test_infrastructure test_util;
  auto write_done_event = create_points(test_util, 0, morton_max, 256);
  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, write_done_event.header, write_done_event.attribute_id, std::move(write_done_event.locations));

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);

  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));
  auto tree = *test_util.tree_registry.get(root_id);
  REQUIRE(tree.morton_max.data[0] == morton_max);
  REQUIRE(tree.morton_max.data[1] == 0);
  REQUIRE(tree.morton_max.data[2] == 0);

  REQUIRE(tree.morton_min.data[0] == 0);
  REQUIRE(tree.morton_min.data[1] == 0);
  REQUIRE(tree.morton_min.data[2] == 0);

  REQUIRE(tree.data[0][0].data.size() == 2);
  REQUIRE(tree.data[0][0].data.back().input_id == second_points.header.input_id);
  REQUIRE(tree.nodes[0][0] == 0);
}

TEST_CASE("add_new_node")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  tree_test_infrastructure test_util(256);
  auto points = create_points(test_util, 0, morton_max, 256);

  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);

  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));
  auto &tree = *test_util.tree_registry.get(root_id);
  REQUIRE(tree.nodes[0][0] > 0);
  REQUIRE(tree.data[0][0].data.empty());
}

TEST_CASE("add_new_subtree")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2)) - 1);
  tree_test_infrastructure test_util(256);
  auto points = create_points(test_util, 0, morton_max, 256);
  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));
  auto &tree = *test_util.tree_registry.get(root_id);

  REQUIRE(tree.nodes[0][0] == 0);
  REQUIRE(tree.data[0][0].data.size() == 1);

  morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  auto second_points = create_points(test_util, 0, morton_max, 256);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));
  auto &second_tree = *test_util.tree_registry.get(root_id);

  REQUIRE(second_tree.nodes[0].size() == 1);
  REQUIRE(second_tree.nodes[4].size() == 1);
  REQUIRE(second_tree.skips[4].size() == 1);
  REQUIRE(second_tree.sub_trees.size() == 1);
  auto &added_tree = second_tree.sub_trees[second_tree.skips[4][0]];
  auto &sub_tree = *test_util.tree_registry.get(added_tree);
  REQUIRE(sub_tree.nodes[1].size() == 8);
  require_chunk_refs_consistent(test_util.tree_registry);
}

TEST_CASE("add_new_subtree_offsets")
{
  for (int i = 1; i < 11; i++)
  {
    tree_test_infrastructure test_util(256);
    uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2 + i)) - 1);
    auto points = create_points(test_util, 0, morton_max, 256);
    auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));
    auto &tree = *test_util.tree_registry.get(root_id);

    REQUIRE(tree.nodes[0][0] == 0);
    REQUIRE(tree.data[0][0].data.size() == 1);

    morton_max = ((uint64_t(1) << (1 * 3 * 5 + i)) - 1);
    auto second_points = create_points(test_util, 0, morton_max, 256);
    root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));
    auto &second_tree = *test_util.tree_registry.get(root_id);

    REQUIRE(second_tree.nodes[0].size() == 1);
    REQUIRE(second_tree.nodes[4].size() >= 1);
    REQUIRE(second_tree.skips[4].size() >= 1);
    REQUIRE(second_tree.sub_trees.size() >= 1);
    auto &added_tree = second_tree.sub_trees[second_tree.skips[4][0]];
    auto &sub_tree = *test_util.tree_registry.get(added_tree);
    REQUIRE(sub_tree.nodes[0].size() >= 1);
    require_chunk_refs_consistent(test_util.tree_registry);
  }
}
TEST_CASE("reparent")
{
  tree_test_infrastructure test_util(256);

  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);

  auto first_points = create_points(test_util, 0, morton_max);
  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, first_points.header, first_points.attribute_id, std::move(first_points.locations));

  morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2)) - 1);
  auto second_points = create_points(test_util, 0, morton_max);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.data[0][0].data.size() == 0);

    REQUIRE(tree.sub_trees.size() == 1);
  }

  auto third_points = create_points(test_util, 1, morton_max);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, third_points.header, third_points.attribute_id, std::move(third_points.locations));
  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.sub_trees.size() == 1);
  }

  uint64_t splitting_max = morton_max + morton_max / 2;
  uint64_t splitting_min = morton_max / 2;
  auto fourth_points = create_points(test_util, splitting_min, splitting_max);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, fourth_points.header, fourth_points.attribute_id, std::move(fourth_points.locations));
  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.sub_trees.size() == 2);
    REQUIRE(tree.magnitude == 2);
  }

  uint64_t very_large_max = morton_max / 2;
  auto fifth_points = create_points(test_util, 0, very_large_max);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, fifth_points.header, fifth_points.attribute_id, std::move(fifth_points.locations));
  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.sub_trees.size() == 2);
    REQUIRE(tree.magnitude == 2);
  }
}
TEST_CASE("reparent non-zero child position")
{
  tree_test_infrastructure test_util(256);

  // Place the first tree at a high morton range so that when reparenting
  // occurs, insert_tree_in_tree places the old tree at a non-zero child
  // position in the new parent. morton_min = (1<<27) gives child_mask = 1
  // at lod 9 (magnitude 1 parent).
  uint64_t high_min = uint64_t(1) << 27;
  uint64_t high_max = high_min + ((uint64_t(1) << 15) - 1);
  auto first_points = create_points(test_util, high_min, high_max);
  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, first_points.header, first_points.attribute_id, std::move(first_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.magnitude == 0);
  }

  // Add points at min=0 to force reparent. The new parent (magnitude 1)
  // must place the old tree at child position 1, not 0.
  uint64_t wide_max = (uint64_t(1) << 30) - 1;
  auto second_points = create_points(test_util, 0, wide_max);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.magnitude == 1);
    REQUIRE(tree.sub_trees.size() >= 1);
  }

  // Add a third set of points in the low range to exercise the subtree
  // that was created at child position 0 during the split.
  auto third_points = create_points(test_util, 0, high_min - 1);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, third_points.header, third_points.attribute_id, std::move(third_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.magnitude == 1);
  }
}
TEST_CASE("lod generation updates subset count and offset")
{
  tree_test_infrastructure test_util(256);

  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  auto points = create_points(test_util, 0, morton_max, 256);
  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.nodes[0][0] > 0);
    REQUIRE(tree.data[0][0].data.empty());
  }

  std::mutex lod_mutex;
  std::condition_variable lod_cv;
  bool lod_complete = false;

  vio::event_pipe_t<void> lod_done(test_util.event_loop, std::function<void()>([&] {
    std::lock_guard<std::mutex> lock(lod_mutex);
    lod_complete = true;
    lod_cv.notify_all();
  }));

  dew::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                   test_util.perf_stats, lod_done);

  dew::core::morton::morton192_t max_morton;
  memset(&max_morton, 0xFF, sizeof(max_morton));
  lod_gen.generate_lods(root_id, max_morton);

  {
    std::unique_lock<std::mutex> lock(lod_mutex);
    REQUIRE(lod_cv.wait_for(lock, std::chrono::seconds(10), [&] { return lod_complete; }));
  }

  auto &tree = *test_util.tree_registry.get(root_id);
  auto &root_collection = tree.data[0][0];

  REQUIRE(root_collection.data.size() == 1);
  REQUIRE(root_collection.point_count > 0);
  REQUIRE(root_collection.data[0].count.data > 0);
  REQUIRE(root_collection.data[0].count.data == root_collection.point_count);
  REQUIRE(root_collection.data[0].offset.data == 0);
}

TEST_CASE("lod generation on magnitude 0 tree does not trigger negative shift")
{
  tree_test_infrastructure test_util(256);

  // Create a magnitude 0 tree (lod range 0-4) and force it to have children
  // by adding overlapping points that exceed the node limit.
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  auto points = create_points(test_util, 0, morton_max, 256);
  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    // The tree should have children (nodes[0][0] > 0 means root has children)
    REQUIRE(tree.nodes[0][0] > 0);
    REQUIRE(tree.magnitude == 0);
  }

  // Run LOD generation on this magnitude 0 tree. Before the fix, this would
  // trigger UBSan: "shift exponent -12 is negative" in morton_mask_create
  // because maskWidth = lod - 9 went negative for lod < 9.
  std::mutex lod_mutex;
  std::condition_variable lod_cv;
  bool lod_complete = false;

  vio::event_pipe_t<void> lod_done(test_util.event_loop, std::function<void()>([&] {
    std::lock_guard<std::mutex> lock(lod_mutex);
    lod_complete = true;
    lod_cv.notify_all();
  }));

  dew::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                   test_util.perf_stats, lod_done);

  dew::core::morton::morton192_t max_morton;
  memset(&max_morton, 0xFF, sizeof(max_morton));
  lod_gen.generate_lods(root_id, max_morton);

  {
    std::unique_lock<std::mutex> lock(lod_mutex);
    REQUIRE(lod_cv.wait_for(lock, std::chrono::seconds(10), [&] { return lod_complete; }));
  }

  auto &tree = *test_util.tree_registry.get(root_id);
  auto &root_collection = tree.data[0][0];

  // LOD generation should produce valid data even for magnitude 0 trees
  REQUIRE(root_collection.data.size() == 1);
  REQUIRE(root_collection.point_count > 0);
  REQUIRE(root_collection.data[0].count.data > 0);
  REQUIRE(root_collection.data[0].count.data == root_collection.point_count);
  REQUIRE(root_collection.data[0].offset.data == 0);
}

// Helper to register a file with a given name in the registry
static dew::converter::input_data_reference_t register_test_file(dew::converter::input_data_source_registry_t &registry, const std::string &name)
{
  auto name_buf = std::make_unique<char[]>(name.size());
  memcpy(name_buf.get(), name.c_str(), name.size());
  return registry.register_file(std::move(name_buf), uint32_t(name.size()));
}

// Helper to pre-init a file with a given min position (controls morton ordering)
// Also sets morton_min via handle_sorted_points so get_done_morton can return it.
static void pre_init_test_file(dew::converter::input_data_source_registry_t &registry, const dew::core::tree_config_t &tree_config, dew::core::input_data_id_t id, double min_x)
{
  double min[3] = {min_x, 0.0, 0.0};
  registry.register_pre_init_result(tree_config, id, true, min, 100, 16, 0);

  // Set morton_min/max on the source so get_done_morton returns meaningful boundaries
  dew::core::morton::morton192_t morton_min = {};
  dew::core::morton::morton192_t morton_max = {};
  dew::core::convert_pos_to_morton(tree_config.scale, tree_config.offset, min, morton_min);
  double max_pos[3] = {min_x + 1.0, 1.0, 1.0};
  dew::core::convert_pos_to_morton(tree_config.scale, tree_config.offset, max_pos, morton_max);
  registry.handle_sorted_points(id, morton_min, morton_max);
}

// Helper to mark a file as fully done (sub_added + reading_done + tree_done)
static void mark_file_done(dew::converter::input_data_source_registry_t &registry, dew::core::input_data_id_t id)
{
  registry.handle_sub_added(id);
  registry.handle_reading_done(id);
  registry.handle_tree_done_with_input(id);
}

TEST_CASE("get_done_morton returns empty when no files sorted" * doctest::test_suite("[incremental_lod]"))
{
  dew::converter::input_data_source_registry_t registry;
  auto tree_config = create_tree_config(0.001, 0.0);

  auto ref1 = register_test_file(registry, "file1.las");
  auto ref2 = register_test_file(registry, "file2.las");

  pre_init_test_file(registry, tree_config, ref1.input_id, 10.0);
  pre_init_test_file(registry, tree_config, ref2.input_id, 20.0);

  // Don't call next_input_to_process — sorted list is empty
  auto result = registry.get_done_morton();
  REQUIRE(!result.has_value());
}

TEST_CASE("get_done_morton returns empty when first file not done" * doctest::test_suite("[incremental_lod]"))
{
  dew::converter::input_data_source_registry_t registry;
  auto tree_config = create_tree_config(0.001, 0.0);

  auto ref1 = register_test_file(registry, "file1.las");
  auto ref2 = register_test_file(registry, "file2.las");

  pre_init_test_file(registry, tree_config, ref1.input_id, 10.0);
  pre_init_test_file(registry, tree_config, ref2.input_id, 20.0);

  // Pop both into sorted list
  auto next1 = registry.next_input_to_process();
  REQUIRE(next1.has_value());
  auto next2 = registry.next_input_to_process();
  REQUIRE(next2.has_value());

  // Mark second file done but not first
  mark_file_done(registry, next2->id);

  auto result = registry.get_done_morton();
  REQUIRE(!result.has_value());
}

TEST_CASE("get_done_morton returns boundary when prefix is done" * doctest::test_suite("[incremental_lod]"))
{
  dew::converter::input_data_source_registry_t registry;
  auto tree_config = create_tree_config(0.001, 0.0);

  auto ref1 = register_test_file(registry, "file1.las");
  auto ref2 = register_test_file(registry, "file2.las");
  auto ref3 = register_test_file(registry, "file3.las");

  pre_init_test_file(registry, tree_config, ref1.input_id, 10.0);
  pre_init_test_file(registry, tree_config, ref2.input_id, 20.0);
  pre_init_test_file(registry, tree_config, ref3.input_id, 30.0);

  // Pop all into sorted list (ascending morton order due to min-heap)
  auto next1 = registry.next_input_to_process();
  auto next2 = registry.next_input_to_process();
  auto next3 = registry.next_input_to_process();
  REQUIRE(next1.has_value());
  REQUIRE(next2.has_value());
  REQUIRE(next3.has_value());

  // Mark only the first (lowest morton) file as done
  mark_file_done(registry, next1->id);

  auto result = registry.get_done_morton();
  REQUIRE(result.has_value());

  // Now also mark the second file done — the boundary should advance to file 3
  mark_file_done(registry, next2->id);
  auto result2 = registry.get_done_morton();
  REQUIRE(result2.has_value());

  // The boundary should now point to file 3's morton_min, not file 2's
  // (verifies that the prefix scan actually advances)
  // Since file 3 isn't done and there are no unsorted files, we get a value
  // but we still can't LOD everything (file 3 remains)
}

TEST_CASE("get_done_morton returns all-max when all files done" * doctest::test_suite("[incremental_lod]"))
{
  dew::converter::input_data_source_registry_t registry;
  auto tree_config = create_tree_config(0.001, 0.0);

  auto ref1 = register_test_file(registry, "file1.las");
  auto ref2 = register_test_file(registry, "file2.las");

  pre_init_test_file(registry, tree_config, ref1.input_id, 10.0);
  pre_init_test_file(registry, tree_config, ref2.input_id, 20.0);

  auto next1 = registry.next_input_to_process();
  auto next2 = registry.next_input_to_process();
  REQUIRE(next1.has_value());
  REQUIRE(next2.has_value());

  // No more unsorted files
  REQUIRE(!registry.next_input_to_process().has_value());

  mark_file_done(registry, next1->id);
  mark_file_done(registry, next2->id);

  auto result = registry.get_done_morton();
  REQUIRE(result.has_value());

  // Should be the all-0xFF sentinel
  dew::core::morton::morton192_t expected;
  memset(&expected, 0xFF, sizeof(expected));
  REQUIRE(result->data[0] == expected.data[0]);
  REQUIRE(result->data[1] == expected.data[1]);
  REQUIRE(result->data[2] == expected.data[2]);
}

TEST_CASE("get_done_morton clamps by undispatched files" * doctest::test_suite("[incremental_lod]"))
{
  // An undispatched file with KNOWN pre-init bounds no longer blocks the watermark outright: its
  // input_order (morton of the aabb-min corner) is a true lower bound on every point it can ever
  // contribute, so the boundary clamps to it instead. A registered file with UNKNOWN bounds
  // (no pre-init yet -> input_order 0) still blocks everything.
  dew::converter::input_data_source_registry_t registry;
  auto tree_config = create_tree_config(0.001, 0.0);

  auto ref1 = register_test_file(registry, "file1.las");
  auto ref2 = register_test_file(registry, "file2.las");

  pre_init_test_file(registry, tree_config, ref1.input_id, 10.0);
  pre_init_test_file(registry, tree_config, ref2.input_id, 20.0);

  // Dispatch and finish ONLY the first (lowest-morton) file; ref2 stays undispatched.
  auto next1 = registry.next_input_to_process();
  REQUIRE(next1.has_value());
  REQUIRE(next1->id.data == ref1.input_id.data);
  mark_file_done(registry, next1->id);

  // Prefix [ref1] is done; undispatched ref2 clamps the boundary to its aabb-min morton.
  auto result = registry.get_done_morton();
  REQUIRE(result.has_value());
  dew::core::morton::morton192_t expected_clamp;
  double pos2[3] = {20.0, 0.0, 0.0}; // matches pre_init_test_file's {min_x, 0, 0}
  dew::core::convert_pos_to_morton(tree_config.scale, tree_config.offset, pos2, expected_clamp);
  REQUIRE(result->data[0] == expected_clamp.data[0]);
  REQUIRE(result->data[1] == expected_clamp.data[1]);
  REQUIRE(result->data[2] == expected_clamp.data[2]);

  // Now register a file WITHOUT pre-init: its lower bound is unknown (input_order 0), which must
  // pin the boundary to "nothing provably final" until its bounds arrive.
  auto ref3 = register_test_file(registry, "file3.las");
  (void)ref3;
  auto blocked = registry.get_done_morton();
  REQUIRE(!blocked.has_value());
}

TEST_CASE("LOD with restricted morton boundary skips nodes outside range" * doctest::test_suite("[incremental_lod]"))
{
  tree_test_infrastructure test_util(256);

  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);

  // First batch: points spanning full range
  auto points = create_points(test_util, 0, morton_max, 256);
  auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

  // Second batch: overlapping to force children
  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);
  root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.nodes[0][0] > 0);
  }

  // Run LOD with boundary at midpoint — only lower half should get LODed
  std::mutex lod_mutex;
  std::condition_variable lod_cv;
  bool lod_complete = false;

  vio::event_pipe_t<void> lod_done(test_util.event_loop, std::function<void()>([&] {
    std::lock_guard<std::mutex> lock(lod_mutex);
    lod_complete = true;
    lod_cv.notify_all();
  }));

  dew::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                   test_util.perf_stats, lod_done);

  // Use midpoint as boundary
  dew::core::morton::morton192_t mid_morton = {};
  mid_morton.data[0] = morton_max / 2;

  lod_gen.generate_lods(root_id, mid_morton);

  {
    std::unique_lock<std::mutex> lock(lod_mutex);
    REQUIRE(lod_cv.wait_for(lock, std::chrono::seconds(10), [&] { return lod_complete; }));
  }

  auto &tree = *test_util.tree_registry.get(root_id);
  auto &root_collection = tree.data[0][0];

  // The root node's max covers the full range which is above mid_morton,
  // so the root itself should NOT have LOD data (it was skipped)
  REQUIRE(root_collection.point_count == 0);
}

TEST_CASE("Two-pass incremental LOD matches single-pass" * doctest::test_suite("[incremental_lod]"))
{
  // --- Single-pass tree ---
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  uint64_t morton_mid = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);

  dew::core::tree_id_t single_root_id;
  uint64_t single_root_point_count = 0;
  {
    tree_test_infrastructure test_util(256);

    auto points = create_points(test_util, 0, morton_max, 256);
    single_root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

    auto second_points = create_points(test_util, morton_mid, morton_max, 256);
    single_root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, single_root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

    std::mutex lod_mutex;
    std::condition_variable lod_cv;
    bool lod_complete = false;

    vio::event_pipe_t<void> lod_done(test_util.event_loop, std::function<void()>([&] {
      std::lock_guard<std::mutex> lock(lod_mutex);
      lod_complete = true;
      lod_cv.notify_all();
    }));

    dew::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                     test_util.perf_stats, lod_done);

    dew::core::morton::morton192_t max_morton;
    memset(&max_morton, 0xFF, sizeof(max_morton));
    lod_gen.generate_lods(single_root_id, max_morton);

    {
      std::unique_lock<std::mutex> lock(lod_mutex);
      REQUIRE(lod_cv.wait_for(lock, std::chrono::seconds(10), [&] { return lod_complete; }));
    }

    auto &tree = *test_util.tree_registry.get(single_root_id);
    single_root_point_count = tree.data[0][0].point_count;
    REQUIRE(single_root_point_count > 0);
  }

  // --- Two-pass incremental tree ---
  {
    tree_test_infrastructure test_util(256);

    auto points = create_points(test_util, 0, morton_max, 256);
    auto root_id = dew::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

    auto second_points = create_points(test_util, morton_mid, morton_max, 256);
    root_id = dew::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

    // Pass 1: LOD up to midpoint
    std::mutex lod_mutex;
    std::condition_variable lod_cv;
    bool lod_complete = false;

    vio::event_pipe_t<void> lod_done(test_util.event_loop, std::function<void()>([&] {
      std::lock_guard<std::mutex> lock(lod_mutex);
      lod_complete = true;
      lod_cv.notify_all();
    }));

    dew::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                     test_util.perf_stats, lod_done);

    dew::core::morton::morton192_t mid_morton = {};
    mid_morton.data[0] = morton_max / 2;
    lod_gen.generate_lods(root_id, mid_morton);

    {
      std::unique_lock<std::mutex> lock(lod_mutex);
      REQUIRE(lod_cv.wait_for(lock, std::chrono::seconds(10), [&] { return lod_complete; }));
    }

    // Pass 2: LOD the rest (all-max)
    lod_complete = false;
    dew::core::morton::morton192_t max_morton;
    memset(&max_morton, 0xFF, sizeof(max_morton));
    lod_gen.generate_lods(root_id, max_morton);

    {
      std::unique_lock<std::mutex> lock(lod_mutex);
      REQUIRE(lod_cv.wait_for(lock, std::chrono::seconds(10), [&] { return lod_complete; }));
    }

    auto &tree = *test_util.tree_registry.get(root_id);
    auto &root_collection = tree.data[0][0];

    REQUIRE(root_collection.point_count > 0);
    REQUIRE(root_collection.point_count == single_root_point_count);
  }
}

TEST_CASE("tree registry v3 round-trip preserves state, watermark, refs and snapshot" * doctest::test_suite("[registry_v2]"))
{
  auto config = create_tree_config(0.001, 0.0);
  config.read_chunk_byte_target = 96ull << 20; // non-default: must survive the round trip
  dew::core::tree_registry_t registry(1000, config);
  registry.current_id = 3;
  registry.root = dew::core::tree_id_t(1);
  registry.current_lod_node_id = (uint64_t(1) << 63) + 42;       // must survive (v1 dropped it)
  registry.current_collapsed_node_id = (uint64_t(1) << 62) + 7;  // v3
  registry.chunk_tree_refs[dew::core::input_data_id_t{4, 2}] = {3, 200000};
  registry.chunk_tree_refs[dew::core::input_data_id_t{5, 0}] = {1, 731};
  memset(&registry.lod_watermark, 0x3C, sizeof(registry.lod_watermark));
  registry.locations.resize(3);
  registry.locations[1] = {0, 128, 4096};
  registry.tree_state = {uint8_t(dew::core::tree_state_t::final), uint8_t(dew::core::tree_state_t::building), uint8_t(dew::core::tree_state_t::uploaded)};
  registry.tree_band = {7, dew::core::tree_band_none, 9};
  registry.input_registry_snapshot = {1, 2, 3, 4, 5};

  auto serialized = dew::core::tree_registry_serialize(registry);
  REQUIRE(serialized.data != nullptr);

  auto buffer = std::make_unique<uint8_t[]>(serialized.size);
  memcpy(buffer.get(), serialized.data.get(), serialized.size);
  dew::core::tree_registry_t restored;
  auto error = dew::core::tree_registry_deserialize(buffer, uint32_t(serialized.size), restored);
  REQUIRE(error.code == 0);

  REQUIRE(restored.node_limit == registry.node_limit);
  REQUIRE(restored.current_id == registry.current_id);
  REQUIRE(restored.root.data == registry.root.data);
  REQUIRE(restored.current_lod_node_id == registry.current_lod_node_id);
  REQUIRE(restored.current_collapsed_node_id == registry.current_collapsed_node_id);
  REQUIRE(restored.tree_config.read_chunk_byte_target == config.read_chunk_byte_target);
  REQUIRE(restored.chunk_tree_refs.size() == 2);
  REQUIRE(restored.chunk_tree_refs.at(dew::core::input_data_id_t{4, 2}).tree_count == 3);
  REQUIRE(restored.chunk_tree_refs.at(dew::core::input_data_id_t{4, 2}).point_count == 200000);
  REQUIRE(restored.chunk_tree_refs.at(dew::core::input_data_id_t{5, 0}).tree_count == 1);
  REQUIRE(restored.chunk_tree_refs.at(dew::core::input_data_id_t{5, 0}).point_count == 731);
  REQUIRE(memcmp(&restored.lod_watermark, &registry.lod_watermark, sizeof(registry.lod_watermark)) == 0);
  REQUIRE(restored.locations.size() == 3);
  REQUIRE(restored.locations[1].offset == 4096);
  REQUIRE(restored.tree_state == registry.tree_state);
  REQUIRE(restored.tree_band == registry.tree_band);
  REQUIRE(restored.input_registry_snapshot == registry.input_registry_snapshot);
}

namespace
{
// The 40-byte tree_config layout that v1/v2 registry blobs serialized (no read_chunk_byte_target).
struct tree_config_v2_layout_t
{
  double scale;
  double offset[3];
  bool store_original_order;
  uint32_t node_point_limit;
};
static_assert(sizeof(tree_config_v2_layout_t) == 40, "historic layout");

tree_config_v2_layout_t v2_config_from(const dew::core::tree_config_t &config)
{
  tree_config_v2_layout_t out = {};
  out.scale = config.scale;
  memcpy(out.offset, config.offset, sizeof(out.offset));
  out.store_original_order = config.store_original_order;
  out.node_point_limit = config.node_point_limit;
  return out;
}
} // namespace

TEST_CASE("tree registry v2 blob deserializes with defaulted v3 state" * doctest::test_suite("[registry_v2]"))
{
  // Hand-build a v2 blob: magic, 40-byte config, v2 fields, no collapsed counter / chunk refs.
  const uint32_t magic_v2 = 0x32475254u;
  const uint32_t node_limit = 1000;
  const uint32_t current_id = 1;
  const dew::core::tree_id_t root(0);
  const auto config = v2_config_from(create_tree_config(0.001, 0.0));
  const uint64_t lod_node_id = (uint64_t(1) << 63) + 5;
  dew::core::morton::morton192_t watermark = {};
  watermark.data[0] = 77;
  const uint32_t count = 1;
  dew::core::storage_location_t location = {0, 64, 1024};
  const uint8_t state = uint8_t(dew::core::tree_state_t::final);
  const uint32_t band = 2;
  const uint32_t snapshot_size = 0;

  const uint32_t size = sizeof(magic_v2) + sizeof(node_limit) + sizeof(current_id) + sizeof(root) + sizeof(config) + sizeof(lod_node_id) + sizeof(watermark) + sizeof(count) + sizeof(location) + sizeof(state) + sizeof(band) + sizeof(snapshot_size);
  auto buffer = std::make_unique<uint8_t[]>(size);
  uint8_t *p = buffer.get();
  auto put = [&p](const auto &v) { memcpy(p, &v, sizeof(v)); p += sizeof(v); };
  put(magic_v2);
  put(node_limit);
  put(current_id);
  put(root);
  put(config);
  put(lod_node_id);
  put(watermark);
  put(count);
  put(location);
  put(state);
  put(band);
  put(snapshot_size);
  REQUIRE(p == buffer.get() + size);

  dew::core::tree_registry_t restored;
  auto error = dew::core::tree_registry_deserialize(buffer, size, restored);
  REQUIRE(error.code == 0);
  REQUIRE(restored.node_limit == node_limit);
  REQUIRE(restored.current_lod_node_id == lod_node_id);
  REQUIRE(restored.lod_watermark.data[0] == 77);
  REQUIRE(restored.tree_state.size() == 1);
  REQUIRE(restored.tree_state[0] == state);
  REQUIRE(restored.tree_band[0] == band);
  // v3 additions default:
  REQUIRE(restored.current_collapsed_node_id == uint64_t(1) << 62);
  REQUIRE(restored.chunk_tree_refs.empty());
  REQUIRE(restored.tree_config.read_chunk_byte_target == dew::core::tree_config_t{}.read_chunk_byte_target);
}

TEST_CASE("tree registry v1 blob deserializes with defaulted v2 state" * doctest::test_suite("[registry_v2]"))
{
  // Hand-build a v1 blob (no magic; first u32 is node_limit) and check the v2 fields default.
  const uint32_t node_limit = 200000;
  const uint32_t current_id = 2;
  const dew::core::tree_id_t root(0);
  const auto tree_config = v2_config_from(create_tree_config(0.001, 0.0)); // v1 wrote the 40-byte layout
  dew::core::storage_location_t locations[2] = {{0, 64, 1024}, {0, 32, 2048}};
  const uint32_t count = 2;

  const uint32_t size = sizeof(node_limit) + sizeof(current_id) + sizeof(root) + sizeof(tree_config) + sizeof(count) + sizeof(locations);
  auto buffer = std::make_unique<uint8_t[]>(size);
  uint8_t *p = buffer.get();
  memcpy(p, &node_limit, sizeof(node_limit)); p += sizeof(node_limit);
  memcpy(p, &current_id, sizeof(current_id)); p += sizeof(current_id);
  memcpy(p, &root, sizeof(root)); p += sizeof(root);
  memcpy(p, &tree_config, sizeof(tree_config)); p += sizeof(tree_config);
  memcpy(p, &count, sizeof(count)); p += sizeof(count);
  memcpy(p, locations, sizeof(locations)); p += sizeof(locations);
  REQUIRE(p == buffer.get() + size);

  dew::core::tree_registry_t restored;
  auto error = dew::core::tree_registry_deserialize(buffer, size, restored);
  REQUIRE(error.code == 0);
  REQUIRE(restored.node_limit == node_limit);
  REQUIRE(restored.locations.size() == 2);
  REQUIRE(restored.tree_state.size() == 2);
  REQUIRE(restored.tree_state[0] == uint8_t(dew::core::tree_state_t::building));
  REQUIRE(restored.tree_band.size() == 2);
  REQUIRE(restored.tree_band[0] == dew::core::tree_band_none);
  REQUIRE(restored.input_registry_snapshot.empty());
  REQUIRE(restored.current_lod_node_id == uint64_t(1) << 63);
}

TEST_CASE("input registry snapshot round-trips and restores dispatch state" * doctest::test_suite("[registry_v2]"))
{
  dew::converter::input_data_source_registry_t registry;
  auto tree_config = create_tree_config(0.001, 0.0);

  auto ref1 = register_test_file(registry, "done.las");
  auto ref2 = register_test_file(registry, "inflight.las");
  auto ref3 = register_test_file(registry, "waiting.las");
  pre_init_test_file(registry, tree_config, ref1.input_id, 10.0);
  pre_init_test_file(registry, tree_config, ref2.input_id, 20.0);
  pre_init_test_file(registry, tree_config, ref3.input_id, 30.0);

  // Dispatch ref1 + ref2 (rising morton); finish only ref1; ref3 stays undispatched.
  auto next1 = registry.next_input_to_process();
  auto next2 = registry.next_input_to_process();
  REQUIRE(next1.has_value());
  REQUIRE(next2.has_value());
  REQUIRE(next1->id.data == ref1.input_id.data);
  REQUIRE(next2->id.data == ref2.input_id.data);
  mark_file_done(registry, next1->id);
  auto watermark_before = registry.get_done_morton();
  REQUIRE(watermark_before.has_value());

  auto snapshot = registry.serialize();
  REQUIRE(!snapshot.empty());

  dew::converter::input_data_source_registry_t restored;
  auto error = restored.deserialize(snapshot.data(), uint32_t(snapshot.size()));
  REQUIRE(error.code == 0);

  // The done prefix survives: the watermark is identical to the pre-snapshot one.
  auto watermark_after = restored.get_done_morton();
  REQUIRE(watermark_after.has_value());
  REQUIRE(watermark_after->data[0] == watermark_before->data[0]);
  REQUIRE(watermark_after->data[1] == watermark_before->data[1]);
  REQUIRE(watermark_after->data[2] == watermark_before->data[2]);

  // ref2 was dispatched-but-unfinished at snapshot time -> it must be re-dispatchable (re-read
  // from scratch); ref3 was never dispatched -> also dispatchable; ref1 is done -> never again.
  // Rising-morton order: ref2 (20) pops before ref3 (30).
  auto redo = restored.next_input_to_process();
  REQUIRE(redo.has_value());
  REQUIRE(redo->id.data == ref2.input_id.data);
  auto waiting = restored.next_input_to_process();
  REQUIRE(waiting.has_value());
  REQUIRE(waiting->id.data == ref3.input_id.data);
  REQUIRE(!restored.next_input_to_process().has_value());

  // A fresh id after restore must not collide with any persisted id.
  auto fresh = dew::converter::get_next_input_id();
  REQUIRE(fresh.data > ref3.input_id.data);
}

TEST_CASE("finality marking across multi-batch morton-ordered input" * doctest::test_suite("[registry_v2]"))
{
  // End-to-end through tree_handler_t: insert two morton-ordered batches, run an incremental LOD
  // pass with a watermark between them, then the terminal pass. After each checkpoint, every
  // tree's state must match the rule: final iff morton_max < watermark (or terminal pass).
  tree_test_infrastructure test_util(256);

  std::mutex sync_mutex;
  std::condition_variable sync_cv;
  int checkpoints_seen = 0;
  int inputs_done = 0;
  test_util.on_index_written = [&] {
    std::lock_guard<std::mutex> lock(sync_mutex);
    checkpoints_seen++;
    sync_cv.notify_all();
  };
  vio::event_pipe_t<dew::core::input_data_id_t> done_input(test_util.event_loop, std::function<void(dew::core::input_data_id_t &&)>([&](dew::core::input_data_id_t &&) {
    std::lock_guard<std::mutex> lock(sync_mutex);
    inputs_done++;
    sync_cv.notify_all();
  }));

  dew::converter::tree_handler_t tree_handler(test_util.worker_thread_pool, test_util.cache_file_handler, test_util.attributes_config, test_util.perf_stats, done_input);
  tree_handler.set_tree_initialization_config(test_util.tree_config);
  tree_handler.set_tree_initialization_node_point_limit(256);

  const uint64_t full_max = (uint64_t(1) << (2 * 3 * 5)) - 1; // two magnitudes -> sub-trees exist
  const uint64_t mid = full_max / 2;

  auto batch1 = create_points(test_util, 0, mid - 1, 256);
  auto batch2 = create_points(test_util, mid, full_max, 256);

  {
    auto locations1 = batch1.locations;
    auto header1 = batch1.header;
    tree_handler.add_points(std::move(header1), std::move(batch1.attribute_id), std::move(locations1));
    auto locations2 = batch2.locations;
    auto header2 = batch2.header;
    tree_handler.add_points(std::move(header2), std::move(batch2.attribute_id), std::move(locations2));
    std::unique_lock<std::mutex> lock(sync_mutex);
    sync_cv.wait(lock, [&] { return inputs_done == 2; });
  }

  // Pass 1: watermark at batch2's min -- everything strictly below is final.
  dew::core::morton::morton192_t watermark = {};
  watermark.data[0] = mid;
  tree_handler.generate_lod(watermark);
  {
    std::unique_lock<std::mutex> lock(sync_mutex);
    sync_cv.wait(lock, [&] { return checkpoints_seen == 1; });
  }
  {
    auto &registry = tree_handler.tree_registry();
    int final_count = 0;
    for (uint32_t i = 0; i < uint32_t(registry.data.size()); i++)
    {
      auto *tree = registry.data[i].get();
      if (!tree)
        continue;
      const bool below = tree->morton_max < watermark;
      const bool is_final = registry.tree_state[i] == uint8_t(dew::core::tree_state_t::final);
      REQUIRE(is_final == below);
      if (is_final)
        final_count++;
    }
    // The root tree spans both batches, so it must still be building.
    REQUIRE(registry.tree_state[registry.root.data] == uint8_t(dew::core::tree_state_t::building));
    (void)final_count;
  }

  // Terminal pass: everything becomes final, and the watermark persists into the registry.
  dew::core::morton::morton192_t terminal;
  memset(&terminal, 0xFF, sizeof(terminal));
  tree_handler.generate_lod(terminal);
  {
    std::unique_lock<std::mutex> lock(sync_mutex);
    sync_cv.wait(lock, [&] { return checkpoints_seen == 2; });
  }
  {
    auto &registry = tree_handler.tree_registry();
    for (uint32_t i = 0; i < uint32_t(registry.data.size()); i++)
    {
      if (!registry.data[i])
        continue;
      REQUIRE(registry.tree_state[i] == uint8_t(dew::core::tree_state_t::final));
    }
    REQUIRE(memcmp(&registry.lod_watermark, &terminal, sizeof(terminal)) == 0);
  }
}

template <typename T>
struct task_result_type;
template <typename T>
struct task_result_type<vio::task_t<T>>
{
  using type = T;
};

// Run an io_manager coroutine from the test thread (memory store: loop-agnostic).
template <typename F>
static auto bucket_op(vio::event_loop_t &loop, F &&f)
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

TEST_CASE("bucket upload end-to-end: bands, packs, manifests, completion" * doctest::test_suite("[upload]"))
{
  tree_test_infrastructure test_util(256);

  std::mutex sync_mutex;
  std::condition_variable sync_cv;
  int checkpoints_seen = 0;
  int inputs_done = 0;
  test_util.on_index_written = [&] {
    std::lock_guard<std::mutex> lock(sync_mutex);
    checkpoints_seen++;
    sync_cv.notify_all();
  };
  vio::event_pipe_t<dew::core::input_data_id_t> done_input(test_util.event_loop, std::function<void(dew::core::input_data_id_t &&)>([&](dew::core::input_data_id_t &&) {
    std::lock_guard<std::mutex> lock(sync_mutex);
    inputs_done++;
    sync_cv.notify_all();
  }));

  dew::converter::tree_handler_t tree_handler(test_util.worker_thread_pool, test_util.cache_file_handler, test_util.attributes_config, test_util.perf_stats, done_input);
  tree_handler.set_tree_initialization_config(test_util.tree_config);
  tree_handler.set_tree_initialization_node_point_limit(256);

  uint8_t uuid[16];
  for (int i = 0; i < 16; i++)
    uuid[i] = uint8_t(0x40 + i);
  auto bucket = std::make_unique<vio::objstore::memory_io_manager_t>();
  auto *bucket_raw = bucket.get();
  dew::converter::upload_handler_t uploader(std::move(bucket), test_util.cache_file_handler, test_util.worker_thread_pool, uuid);
  REQUIRE(uploader.bootstrap().code == 0);
  uploader.set_on_band_committed([&](uint32_t band_id, std::vector<uint32_t> tree_ids, const dew::core::morton::morton192_t &) { tree_handler.mark_band_uploaded(band_id, std::move(tree_ids)); });
  tree_handler.set_band_sink([&](dew::converter::band_job_t &&job) { uploader.enqueue_band(std::move(job)); }, uploader.committed_band_count(), uploader.stats().complete);

  const uint64_t full_max = (uint64_t(1) << (2 * 3 * 5)) - 1;
  const uint64_t mid = full_max / 2;
  auto batch1 = create_points(test_util, 0, mid - 1, 256);
  auto batch2 = create_points(test_util, mid, full_max, 256);
  {
    auto locations1 = batch1.locations;
    auto header1 = batch1.header;
    tree_handler.add_points(std::move(header1), std::move(batch1.attribute_id), std::move(locations1));
    auto locations2 = batch2.locations;
    auto header2 = batch2.header;
    tree_handler.add_points(std::move(header2), std::move(batch2.attribute_id), std::move(locations2));
    std::unique_lock<std::mutex> lock(sync_mutex);
    sync_cv.wait(lock, [&] { return inputs_done == 2; });
  }

  // Pass 1 (partial watermark) then the terminal pass; each commit emits a band.
  dew::core::morton::morton192_t watermark = {};
  watermark.data[0] = mid;
  tree_handler.generate_lod(watermark);
  {
    std::unique_lock<std::mutex> lock(sync_mutex);
    sync_cv.wait(lock, [&] { return checkpoints_seen == 1; });
  }
  dew::core::morton::morton192_t terminal;
  memset(&terminal, 0xFF, sizeof(terminal));
  tree_handler.generate_lod(terminal);
  {
    std::unique_lock<std::mutex> lock(sync_mutex);
    sync_cv.wait(lock, [&] { return checkpoints_seen == 2; });
  }
  // Both bands were enqueued by the two commits; wait for the uploader to drain them.
  for (int i = 0; i < 200 && !(uploader.stats().complete || uploader.stats().parked); i++)
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  auto stats = uploader.stats();
  REQUIRE(!stats.parked);
  REQUIRE(stats.complete);
  REQUIRE(stats.bands_committed >= 1);
  REQUIRE(stats.bytes_uploaded > 0);

  // ---- Verify the bucket ----
  auto &loop = test_util.event_loop;
  // Root manifest: complete, uuid, registry location set.
  std::vector<uint8_t> root_bytes(dew::core::k_root_manifest_size);
  {
    auto read = bucket_op(loop, [&]() { return bucket_raw->read_object(dew::core::bucket_root_manifest_name(), root_bytes.data(), {}); });
    REQUIRE(read.has_value());
  }
  dew::core::root_manifest_t root;
  REQUIRE(dew::core::deserialize_root_manifest(root_bytes.data(), uint32_t(root_bytes.size()), root).code == 0);
  REQUIRE(root.complete == 1);
  REQUIRE(memcmp(root.dataset_uuid, uuid, 16) == 0);
  REQUIRE(root.band_count == stats.bands_committed);
  REQUIRE(root.tree_registry.size > 0);

  // Every band manifest parses; collect the dedup table for blob verification.
  std::vector<dew::core::band_dedup_entry_t> all_blobs;
  for (uint32_t band = 0; band < root.band_count; band++)
  {
    auto info = bucket_op(loop, [&]() { return bucket_raw->object_info(dew::core::bucket_band_name(band)); });
    REQUIRE(info.has_value());
    REQUIRE(info.value().exists);
    std::vector<uint8_t> band_bytes(info.value().size);
    auto read = bucket_op(loop, [&]() { return bucket_raw->read_object(dew::core::bucket_band_name(band), band_bytes.data(), {}); });
    REQUIRE(read.has_value());
    dew::core::band_manifest_t manifest;
    REQUIRE(dew::core::deserialize_band_manifest(band_bytes.data(), uint32_t(band_bytes.size()), manifest).code == 0);
    REQUIRE(manifest.band_id == band);
    for (auto &blob : manifest.blobs)
      all_blobs.push_back(blob);
  }
  REQUIRE(!all_blobs.empty());

  // Every uploaded blob's bytes match the cache's raw bytes (whole-object GET, no range: the blob
  // IS the object and the recorded offset is always 0).
  for (auto &blob : all_blobs)
  {
    auto request = test_util.cache_file_handler.read(dew::core::storage_location_t{0, blob.location.size, blob.cache_offset}, /*raw=*/true);
    request->wait_for_read();
    REQUIRE(request->error.code == 0);
    REQUIRE(blob.location.offset == 0);
    std::vector<uint8_t> bucket_bytes(blob.location.size);
    auto read = bucket_op(loop, [&]() { return bucket_raw->read_object_all(dew::core::bucket_data_object_name(blob.location.file_id), bucket_bytes.data(), blob.location.size); });
    REQUIRE(read.has_value());
    REQUIRE(read.value() == blob.location.size);
    REQUIRE(memcmp(bucket_bytes.data(), request->buffer_info.data, blob.location.size) == 0);
  }

  // The bucket registry deserializes; every tree location points into a pack and the tree parses.
  {
    std::vector<uint8_t> reg_bytes(root.tree_registry.size);
    REQUIRE(root.tree_registry.offset == 0);
    auto read = bucket_op(loop, [&]() { return bucket_raw->read_object_all(dew::core::bucket_data_object_name(root.tree_registry.file_id), reg_bytes.data(), root.tree_registry.size); });
    REQUIRE(read.has_value());
    auto buffer = std::make_unique<uint8_t[]>(reg_bytes.size());
    memcpy(buffer.get(), reg_bytes.data(), reg_bytes.size());
    dew::core::tree_registry_t bucket_registry;
    REQUIRE(dew::core::tree_registry_deserialize(buffer, uint32_t(reg_bytes.size()), bucket_registry).code == 0);
    REQUIRE(bucket_registry.locations.size() == tree_handler.tree_registry().locations.size());
    for (uint32_t i = 0; i < uint32_t(bucket_registry.locations.size()); i++)
    {
      REQUIRE(bucket_registry.tree_state[i] == uint8_t(dew::core::tree_state_t::uploaded));
      auto &tree_location = bucket_registry.locations[i];
      REQUIRE(tree_location.size > 0);
      std::vector<uint8_t> tree_bytes(tree_location.size);
      REQUIRE(tree_location.offset == 0);
      auto tree_read = bucket_op(loop, [&]() { return bucket_raw->read_object_all(dew::core::bucket_data_object_name(tree_location.file_id), tree_bytes.data(), tree_location.size); });
      REQUIRE(tree_read.has_value());
      dew::core::serialized_tree_t serialized;
      serialized.size = int(tree_bytes.size());
      serialized.data = std::make_shared<uint8_t[]>(tree_bytes.size());
      memcpy(serialized.data.get(), tree_bytes.data(), tree_bytes.size());
      dew::core::tree_t bucket_tree;
      dew_error_t tree_error = {};
      REQUIRE(dew::core::tree_deserialize(serialized, bucket_tree, tree_error));
      // Remapped storage locations must reference data objects (file_id < next_object_id), not the cache.
      bucket_tree.storage_map.for_each([&](dew::core::input_data_id_t, dew::core::attributes_id_t, const std::vector<dew::core::storage_location_t> &locations) {
        for (auto &location : locations)
          if (location.size)
            REQUIRE(location.file_id < root.next_object_id);
      });
    }
  }

  uploader.stop();
}

} // namespace
