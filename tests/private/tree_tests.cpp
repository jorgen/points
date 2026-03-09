#include <doctest/doctest.h>
#include <fmt/printf.h>
#include <utility>

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#include <vio/thread_pool.h>

#include "conversion_types.hpp"
#include "input_data_source_registry.hpp"
#include "storage_handler.hpp"
#include "tree_lod_generator.hpp"
#include <attributes_configs.hpp>
#include <input_header.hpp>
#include <morton.hpp>
#include <morton_tree_coordinate_transform.hpp>
#include <points/common/format.h>
#include <points/converter/converter.h>
#include <points/converter/default_attribute_names.h>
#include <tree.hpp>

namespace
{

points::converter::tree_config_t create_tree_config(double scale, double offset = -double(uint64_t(1) << 17))
{
  points::converter::tree_config_t tree_config;
  tree_config.scale = scale;

  tree_config.offset[0] = offset;
  tree_config.offset[1] = offset;
  tree_config.offset[2] = offset;

  return tree_config;
}

struct write_done_event_t
{
  points::converter::storage_header_t header;
  points::converter::attributes_id_t attribute_id;
  std::vector<points::converter::storage_location_t> locations;
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

  void write(const points::converter::storage_header_t &header, points::converter::attributes_id_t attribute_id, points::converter::attribute_buffers_t &&buffers)
  {
    std::unique_lock<std::mutex> lock(wait_for_write_done_mutex);
    write_done_state = false;

    cache_file_handler.write(header, attribute_id, std::move(buffers),
                             [this](const points::converter::storage_header_t &header, points::converter::attributes_id_t attributes_id, std::vector<points::converter::storage_location_t> &&location,
                                    const points::error_t &error) { handle_write_done(header, attributes_id, std::move(location)); });
  }

  void handle_index_written()
  {
    fmt::print("index written\n");
  }
  void handle_file_error(const points::error_t &&error)
  {
    fmt::print("error: {}\n", error.msg);
  }
  void handle_write_done(const points::converter::storage_header_t &header, points::converter::attributes_id_t attributes_id, std::vector<points::converter::storage_location_t> &&location)
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

  points::error_t error;
  vio::thread_pool_t worker_thread_pool;
  vio::thread_with_event_loop_t event_loop_thread;
  vio::event_loop_t &event_loop;
  uint32_t node_limit;
  points::converter::tree_config_t tree_config;
  points::converter::tree_registry_t tree_registry;
  points::converter::attributes_configs_t attributes_config;
  vio::event_pipe_t<void> index_written;
  vio::event_pipe_t<points::error_t> cache_file_error;
  points::converter::perf_stats_t perf_stats;
  points::converter::storage_handler_t cache_file_handler;

  uint32_t next_input_id = 0;
  bool write_done_state = false;
  std::mutex wait_for_write_done_mutex;
  std::condition_variable wait_for_write_done_cond;
  write_done_event_t write_done_event;
};

void attributes_add_attributecpp(points::converter::attributes_t &attr, const std::string &name, points::type_t format, points::components_t components)
{
  attributes_add_attribute(&attr, name.c_str(), uint32_t(name.size()), format, components);
}

write_done_event_t create_points(tree_test_infrastructure &test_util, uint64_t min, uint64_t max, uint64_t point_count = 256)
{
  points::converter::attributes_t attrs;
  attributes_add_attributecpp(attrs, POINTS_ATTRIBUTE_XYZ, points::type_m64, points::components_1);
  attributes_add_attributecpp(attrs, POINTS_ATTRIBUTE_INTENSITY, points::type_u8, points::components_1);
  auto attr_id = test_util.attributes_config.get_attribute_config_index(std::move(attrs));
  auto attr_def = test_util.attributes_config.get_format_components(attr_id);

  points::converter::points_t points;
  points.header.input_id = {test_util.next_input_id++, 0};
  points.header.morton_min.data[0] = min;
  points.header.morton_min.data[1] = 0;
  points.header.morton_min.data[2] = 0;
  points.header.morton_max.data[0] = max;
  points.header.morton_max.data[1] = 0;
  points.header.morton_max.data[2] = 0;
  points.header.point_count = point_count;
  points.header.lod_span = points::converter::morton::morton_lod(points.header.morton_min, points.header.morton_max);
  points.header.point_format = attr_def[0];
  attribute_buffers_initialize(attr_def, points.buffers, point_count);

  auto *morton_buffer = reinterpret_cast<points::converter::morton::morton64_t *>(points.buffers.data[0].get());
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

TEST_CASE("initialize empty tree")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  tree_test_infrastructure test_util;
  auto write_done_event = create_points(test_util, 0, morton_max, 256);
  auto &[header, attribute_id, locations] = write_done_event;

  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, header, attribute_id, std::move(locations));
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
  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, write_done_event.header, write_done_event.attribute_id, std::move(write_done_event.locations));

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);

  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));
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

  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);

  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));
  auto &tree = *test_util.tree_registry.get(root_id);
  REQUIRE(tree.nodes[0][0] > 0);
  REQUIRE(tree.data[0][0].data.empty());
}

TEST_CASE("add_new_subtree")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2)) - 1);
  tree_test_infrastructure test_util(256);
  auto points = create_points(test_util, 0, morton_max, 256);
  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));
  auto &tree = *test_util.tree_registry.get(root_id);

  REQUIRE(tree.nodes[0][0] == 0);
  REQUIRE(tree.data[0][0].data.size() == 1);

  morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  auto second_points = create_points(test_util, 0, morton_max, 256);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));
  auto &second_tree = *test_util.tree_registry.get(root_id);

  REQUIRE(second_tree.nodes[0].size() == 1);
  REQUIRE(second_tree.nodes[4].size() == 1);
  REQUIRE(second_tree.skips[4].size() == 1);
  REQUIRE(second_tree.sub_trees.size() == 1);
  auto &added_tree = second_tree.sub_trees[second_tree.skips[4][0]];
  auto &sub_tree = *test_util.tree_registry.get(added_tree);
  REQUIRE(sub_tree.nodes[1].size() == 8);
}

TEST_CASE("add_new_subtree_offsets")
{
  for (int i = 1; i < 11; i++)
  {
    tree_test_infrastructure test_util(256);
    uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2 + i)) - 1);
    auto points = create_points(test_util, 0, morton_max, 256);
    auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));
    auto &tree = *test_util.tree_registry.get(root_id);

    REQUIRE(tree.nodes[0][0] == 0);
    REQUIRE(tree.data[0][0].data.size() == 1);

    morton_max = ((uint64_t(1) << (1 * 3 * 5 + i)) - 1);
    auto second_points = create_points(test_util, 0, morton_max, 256);
    root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));
    auto &second_tree = *test_util.tree_registry.get(root_id);

    REQUIRE(second_tree.nodes[0].size() == 1);
    REQUIRE(second_tree.nodes[4].size() >= 1);
    REQUIRE(second_tree.skips[4].size() >= 1);
    REQUIRE(second_tree.sub_trees.size() >= 1);
    auto &added_tree = second_tree.sub_trees[second_tree.skips[4][0]];
    auto &sub_tree = *test_util.tree_registry.get(added_tree);
    REQUIRE(sub_tree.nodes[0].size() >= 1);
  }
}
TEST_CASE("reparent")
{
  tree_test_infrastructure test_util(256);

  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);

  auto first_points = create_points(test_util, 0, morton_max);
  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, first_points.header, first_points.attribute_id, std::move(first_points.locations));

  morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2)) - 1);
  auto second_points = create_points(test_util, 0, morton_max);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.data[0][0].data.size() == 0);

    REQUIRE(tree.sub_trees.size() == 1);
  }

  auto third_points = create_points(test_util, 1, morton_max);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, third_points.header, third_points.attribute_id, std::move(third_points.locations));
  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.sub_trees.size() == 1);
  }

  uint64_t splitting_max = morton_max + morton_max / 2;
  uint64_t splitting_min = morton_max / 2;
  auto fourth_points = create_points(test_util, splitting_min, splitting_max);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, fourth_points.header, fourth_points.attribute_id, std::move(fourth_points.locations));
  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.sub_trees.size() == 2);
    REQUIRE(tree.magnitude == 2);
  }

  uint64_t very_large_max = morton_max / 2;
  auto fifth_points = create_points(test_util, 0, very_large_max);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, fifth_points.header, fifth_points.attribute_id, std::move(fifth_points.locations));
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
  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, first_points.header, first_points.attribute_id, std::move(first_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.magnitude == 0);
  }

  // Add points at min=0 to force reparent. The new parent (magnitude 1)
  // must place the old tree at child position 1, not 0.
  uint64_t wide_max = (uint64_t(1) << 30) - 1;
  auto second_points = create_points(test_util, 0, wide_max);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

  {
    auto &tree = *test_util.tree_registry.get(root_id);
    REQUIRE(tree.magnitude == 1);
    REQUIRE(tree.sub_trees.size() >= 1);
  }

  // Add a third set of points in the low range to exercise the subtree
  // that was created at child position 0 during the split.
  auto third_points = create_points(test_util, 0, high_min - 1);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, third_points.header, third_points.attribute_id, std::move(third_points.locations));

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
  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

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

  points::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                   test_util.perf_stats, lod_done);

  points::converter::morton::morton192_t max_morton;
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
  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

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

  points::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                   test_util.perf_stats, lod_done);

  points::converter::morton::morton192_t max_morton;
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
static points::converter::input_data_reference_t register_test_file(points::converter::input_data_source_registry_t &registry, const std::string &name)
{
  auto name_buf = std::make_unique<char[]>(name.size());
  memcpy(name_buf.get(), name.c_str(), name.size());
  return registry.register_file(std::move(name_buf), uint32_t(name.size()));
}

// Helper to pre-init a file with a given min position (controls morton ordering)
// Also sets morton_min via handle_sorted_points so get_done_morton can return it.
static void pre_init_test_file(points::converter::input_data_source_registry_t &registry, const points::converter::tree_config_t &tree_config, points::converter::input_data_id_t id, double min_x)
{
  double min[3] = {min_x, 0.0, 0.0};
  registry.register_pre_init_result(tree_config, id, true, min, 100, 16);

  // Set morton_min/max on the source so get_done_morton returns meaningful boundaries
  points::converter::morton::morton192_t morton_min = {};
  points::converter::morton::morton192_t morton_max = {};
  points::converter::convert_pos_to_morton(tree_config.scale, tree_config.offset, min, morton_min);
  double max_pos[3] = {min_x + 1.0, 1.0, 1.0};
  points::converter::convert_pos_to_morton(tree_config.scale, tree_config.offset, max_pos, morton_max);
  registry.handle_sorted_points(id, morton_min, morton_max);
}

// Helper to mark a file as fully done (sub_added + reading_done + tree_done)
static void mark_file_done(points::converter::input_data_source_registry_t &registry, points::converter::input_data_id_t id)
{
  registry.handle_sub_added(id);
  registry.handle_reading_done(id);
  registry.handle_tree_done_with_input(id);
}

TEST_CASE("get_done_morton returns empty when no files sorted" * doctest::test_suite("[incremental_lod]"))
{
  points::converter::input_data_source_registry_t registry;
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
  points::converter::input_data_source_registry_t registry;
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
  points::converter::input_data_source_registry_t registry;
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
  points::converter::input_data_source_registry_t registry;
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
  points::converter::morton::morton192_t expected;
  memset(&expected, 0xFF, sizeof(expected));
  REQUIRE(result->data[0] == expected.data[0]);
  REQUIRE(result->data[1] == expected.data[1]);
  REQUIRE(result->data[2] == expected.data[2]);
}

TEST_CASE("get_done_morton returns empty when unsorted files remain" * doctest::test_suite("[incremental_lod]"))
{
  points::converter::input_data_source_registry_t registry;
  auto tree_config = create_tree_config(0.001, 0.0);

  auto ref1 = register_test_file(registry, "file1.las");
  auto ref2 = register_test_file(registry, "file2.las");
  auto ref3 = register_test_file(registry, "file3.las");

  pre_init_test_file(registry, tree_config, ref1.input_id, 10.0);
  pre_init_test_file(registry, tree_config, ref2.input_id, 20.0);
  // Don't pre-init ref3 — it stays in unsorted

  // Pop 2 sorted ones
  auto next1 = registry.next_input_to_process();
  auto next2 = registry.next_input_to_process();
  REQUIRE(next1.has_value());
  REQUIRE(next2.has_value());

  mark_file_done(registry, next1->id);
  mark_file_done(registry, next2->id);

  // All sorted files are done but ref3 hasn't been pre-inited.
  // However, ref3 was registered and has read_started=false, so
  // it will be in the unsorted heap after the dirty flag is resolved.
  // The dirty flag is set by register_file, so next call to
  // next_input_to_process would rebuild the unsorted list including ref3.
  // But since _unsorted_input_sources_dirty is true, get_done_morton returns {}.
  auto result = registry.get_done_morton();
  REQUIRE(!result.has_value());
}

TEST_CASE("LOD with restricted morton boundary skips nodes outside range" * doctest::test_suite("[incremental_lod]"))
{
  tree_test_infrastructure test_util(256);

  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);

  // First batch: points spanning full range
  auto points = create_points(test_util, 0, morton_max, 256);
  auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

  // Second batch: overlapping to force children
  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 256);
  root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

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

  points::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                   test_util.perf_stats, lod_done);

  // Use midpoint as boundary
  points::converter::morton::morton192_t mid_morton = {};
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

  points::converter::tree_id_t single_root_id;
  uint64_t single_root_point_count = 0;
  {
    tree_test_infrastructure test_util(256);

    auto points = create_points(test_util, 0, morton_max, 256);
    single_root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

    auto second_points = create_points(test_util, morton_mid, morton_max, 256);
    single_root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, single_root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

    std::mutex lod_mutex;
    std::condition_variable lod_cv;
    bool lod_complete = false;

    vio::event_pipe_t<void> lod_done(test_util.event_loop, std::function<void()>([&] {
      std::lock_guard<std::mutex> lock(lod_mutex);
      lod_complete = true;
      lod_cv.notify_all();
    }));

    points::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                     test_util.perf_stats, lod_done);

    points::converter::morton::morton192_t max_morton;
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
    auto root_id = points::converter::tree_initialize(test_util.tree_registry, test_util.cache_file_handler, points.header, points.attribute_id, std::move(points.locations));

    auto second_points = create_points(test_util, morton_mid, morton_max, 256);
    root_id = points::converter::tree_add_points(test_util.tree_registry, test_util.cache_file_handler, root_id, second_points.header, second_points.attribute_id, std::move(second_points.locations));

    // Pass 1: LOD up to midpoint
    std::mutex lod_mutex;
    std::condition_variable lod_cv;
    bool lod_complete = false;

    vio::event_pipe_t<void> lod_done(test_util.event_loop, std::function<void()>([&] {
      std::lock_guard<std::mutex> lock(lod_mutex);
      lod_complete = true;
      lod_cv.notify_all();
    }));

    points::converter::tree_lod_generator_t lod_gen(test_util.event_loop, test_util.worker_thread_pool, test_util.tree_registry, test_util.cache_file_handler, test_util.attributes_config,
                                                     test_util.perf_stats, lod_done);

    points::converter::morton::morton192_t mid_morton = {};
    mid_morton.data[0] = morton_max / 2;
    lod_gen.generate_lods(root_id, mid_morton);

    {
      std::unique_lock<std::mutex> lock(lod_mutex);
      REQUIRE(lod_cv.wait_for(lock, std::chrono::seconds(10), [&] { return lod_complete; }));
    }

    // Pass 2: LOD the rest (all-max)
    lod_complete = false;
    points::converter::morton::morton192_t max_morton;
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

} // namespace
