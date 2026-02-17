#include <catch2/catch.hpp>
#include <fmt/printf.h>
#include <utility>

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#include <vio/thread_pool.h>

#include "conversion_types.hpp"
#include "storage_handler.hpp"
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

    , cache_file_handler("test_cache_file", worker_thread_pool, attributes_config, index_written, cache_file_error, error)
  {
    event_loop.add_about_to_block_listener(this);
    cache_file_handler.upgrade_to_write(true);
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

TEST_CASE("initialize empty tree", "[converter, tree_t]")
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

TEST_CASE("add inclusion", "[converter, tree_t]")
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

TEST_CASE("add_new_node", "[converter, tree_t]")
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

TEST_CASE("add_new_subtree", "[converter, tree_t]")
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

TEST_CASE("add_new_subtree_offsets", "[converter, tree_t]")
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
TEST_CASE("reparent", "[converter, tree_t]")
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
TEST_CASE("reparent non-zero child position", "[converter, tree_t]")
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
} // namespace
