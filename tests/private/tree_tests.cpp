#include <catch2/catch.hpp>
#include <fmt/printf.h>
#include <utility>

#include "cache_file_handler.hpp"
#include <attributes_configs.hpp>
#include <threaded_event_loop.hpp>
#include <event_pipe.hpp>
#include <input_header.hpp>
#include <morton.hpp>
#include <morton_tree_coordinate_transform.hpp>
#include <points/common/format.h>
#include <points/converter/converter.h>
#include <points/converter/default_attribute_names.h>
#include <tree.hpp>

namespace
{

points::converter::tree_global_state_t create_tree_global_state(uint32_t node_limit, double scale, double offset = -double(uint64_t(1) << 17))
{
  points::converter::tree_global_state_t globalState;
  globalState.node_limit = node_limit;
  globalState.scale = scale;

  globalState.offset[0] = offset;
  globalState.offset[1] = offset;
  globalState.offset[2] = offset;

  return globalState;
}

struct tree_test_infrastructure : points::converter::about_to_block_t
{
  tree_test_infrastructure(uint32_t node_limit = 1000)
    : global_state(create_tree_global_state(node_limit, 0.001, 0.0))
    , attributes_config(global_state)
    , cache_file_error(event_loop, bind(&tree_test_infrastructure::handle_file_error))
    , write_done(event_loop, bind(&tree_test_infrastructure::handle_write_done))
    , cache_file_handler(global_state, "test_cache_file", attributes_config, cache_file_error, write_done)
  {
    event_loop.add_about_to_block_listener(this);
  }

  std::vector<points::converter::request_id_t> write(const points::converter::storage_header_t &header, points::converter::attribute_buffers_t &&buffers, std::function<void(points::converter::request_id_t id, const points::error_t &error)> on_done)
  {
    std::unique_lock<std::mutex> lock(wait_for_write_done_mutex);
    write_done_state = false;
    return cache_file_handler.write(header, std::move(buffers), std::move(on_done));
  }

  void handle_file_error(const points::error_t &error)
  {
    fmt::print("error: {}\n", error.msg);
  }
  void handle_write_done(const std::pair<points::converter::storage_location_t, points::converter::storage_header_t> &done)
  {
    std::unique_lock<std::mutex> lock(wait_for_write_done_mutex);
    write_done_state = true;
    fmt::print("write done: {}\n", done.second.input_id.data);
    wait_for_write_done_cond.notify_all();
  }

  void wait_for_write_done()
  {
    std::unique_lock<std::mutex> lock(wait_for_write_done_mutex);
    wait_for_write_done_cond.wait(lock, [&] { return write_done_state; });
  }

  void about_to_block() override
  {}

  points::converter::threaded_event_loop_t event_loop;
  points::converter::tree_global_state_t global_state;
  points::converter::attributes_configs_t attributes_config;
  points::converter::event_pipe_t<points::error_t> cache_file_error;
  points::converter::event_pipe_t<std::pair<points::converter::storage_location_t, points::converter::storage_header_t>> write_done;
  points::converter::cache_file_handler_t cache_file_handler;

  bool write_done_state = false;
  std::mutex wait_for_write_done_mutex;
  std::condition_variable wait_for_write_done_cond;
};

void attributes_add_attributecpp(points::converter::attributes_t  &attr, const std::string &name, points::type_t format, points::components_t components)
{
  attributes_add_attribute(&attr, name.c_str(), uint32_t(name.size()), format, components);
}

points::converter::points_t create_points(tree_test_infrastructure &test_util, uint64_t min, uint64_t max, double scale, double offset = 0.0, uint64_t point_count = 256)
{
  points::converter::attributes_t attrs;
  attributes_add_attributecpp(attrs, POINTS_ATTRIBUTE_XYZ, points::type_m64, points::components_1);
  attributes_add_attributecpp(attrs, POINTS_ATTRIBUTE_INTENSITY, points::type_u8, points::components_1);
  auto attr_id = test_util.attributes_config.get_attribute_config_index(std::move(attrs));
  auto attr_def = test_util.attributes_config.get_format_components(attr_id);
 
  points::converter::points_t points;
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
  uint64_t step_size = std::max((points.header.morton_max.data[0] - points.header.morton_min.data[0]) / (point_count - 1), uint64_t(1));
  uint64_t last_value = min;
  for (int i = 0; i < point_count; i++, last_value += step_size)
  {
    morton_buffer[i].data[0] = last_value;
    intensity_buffer[i] = uint8_t(i);
  }
  return points;
}

TEST_CASE("initialize empty tree", "[converter, tree_t]")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  tree_test_infrastructure test_util;
  auto points = create_points(test_util, 0, morton_max, 0.001, 0.0, 256);

  points::converter::tree_cache_t tree_cache;
  auto root_id = points::converter::tree_initialize(test_util.global_state, tree_cache, test_util.cache_file_handler, points.header);
  auto tree = *tree_cache.get(root_id);
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
  points::converter::tree_cache_t tree_cache;
  auto points = create_points(test_util, 0, morton_max, 0.001, 0.0, 256);

  auto root_id = points::converter::tree_initialize(test_util.global_state, tree_cache, test_util.cache_file_handler, points.header);

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 0.001, 0.0, 256);

  root_id = points::converter::tree_add_points(test_util.global_state, tree_cache, test_util.cache_file_handler, root_id, second_points.header);
  auto tree = *tree_cache.get(root_id);
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
  points::converter::tree_cache_t tree_cache;
  auto points = create_points(test_util, 0, morton_max, 0.001, 0.0, 256);
  test_util.write(points.header, std::move(points.buffers), [](points::converter::request_id_t id, const points::error_t &error) { fmt::print("error: {}\n", error.msg); });
  test_util.wait_for_write_done();

  auto root_id = points::converter::tree_initialize(test_util.global_state, tree_cache, test_util.cache_file_handler, points.header);

  uint64_t morton_min = ((uint64_t(1) << (1 * 3 * 5 - 1)) - 1);
  auto second_points = create_points(test_util, morton_min, morton_max, 0.001, 0.0, 256);
  test_util.write(second_points.header, std::move(second_points.buffers), [](points::converter::request_id_t id, const points::error_t &error) { fmt::print("error: {}\n", error.msg); });
  test_util.wait_for_write_done();

  root_id = points::converter::tree_add_points(test_util.global_state, tree_cache, test_util.cache_file_handler, root_id, second_points.header);
  auto tree = *tree_cache.get(root_id);
  REQUIRE(tree.nodes[0][0] > 0);
  REQUIRE(tree.data[0][0].data.empty());
}

//test_case("add_new_subtree", "[converter, tree_t]")
//{
//  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2)) - 1);
//  auto tree_gs = create_tree_global_state(256, 0.001);
//  auto points = create_points(tree_gs, 0, morton_max, 0.001);
//  points::converter::tree_t tree;
//  points::converter::tree_initialize(tree_gs, tree, std::move(points));
//
//  require(tree.nodes[0][0] == 0);
//  require(tree.data[0][0].data.size() == 1);
//
//  morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
//  auto second_points = create_points(tree_gs, 0, morton_max, 0.001);
//  void *buffer_ptr = second_points.buffers.buffers.back().data;
//  points::converter::tree_add_points(tree_gs, tree, std::move(second_points));
//
//  require(tree.nodes[0].size() == 1);
//  require(tree.nodes[1].size() == 8);
//
//}
//test_case("add_new_subtree_offsets", "[converter, tree_t]")
//{
//  for (int i = 1; i < 11; i++)
//  {
//    uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2)) - 1);
//    auto tree_gs = create_tree_global_state(256, 0.001);
//    auto points = create_points(tree_gs, 0, morton_max, 0.001);
//    points::converter::tree_t tree;
//    points::converter::tree_initialize(tree_gs, tree, std::move(points));
//
//    require(tree.nodes[0][0] == 0);
//    require(tree.data[0][0].data.size() == 1);
//
//    morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
//    double scale = 0.001;
//    uint64_t offset = (uint64_t(1) << 6) * i ;
//    auto second_points = create_points(tree_gs, 0, morton_max, scale, offset * scale);
//    points::converter::tree_add_points(tree_gs, tree, std::move(second_points));
//
//    require(tree.nodes[0].size() == 1);
//    require(tree.nodes[1].size() == 8);
//  }
//}
//test_case("reparent", "[converter, tree_t]")
//{
//  auto tree_gs = create_tree_global_state(256, 0.001);
//
//  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
//
//  auto first_points = create_points(tree_gs, 0, morton_max, 0.001);
//  points::converter::tree_t tree;
//  points::converter::tree_initialize(tree_gs, tree, std::move(first_points));
//
//  morton_max = ((uint64_t(1) << (1 * 3 * 5 * 2)) - 1);
//  auto second_points = create_points(tree_gs, 0, morton_max, 0.001);
//  points::converter::tree_add_points(tree_gs, tree, std::move(second_points));
//
//  require(tree.data[0][0].data.size() == 0);
//
//  require(tree.sub_trees.size() == 1);
//  auto third_points = create_points(tree_gs, 1, morton_max, 0.001);
//  points::converter::tree_add_points(tree_gs, tree, std::move(third_points));
//  require(tree.sub_trees.size() == 1);
//
//  uint64_t splitting_max = morton_max + morton_max / 2;
//  uint64_t splitting_min = morton_max / 2;
//  auto fourth_points = create_points(tree_gs, splitting_min, splitting_max, 0.001);
//  points::converter::tree_add_points(tree_gs, tree, std::move(fourth_points));
//  require(tree.sub_trees.size() == 2);
//  require(tree.magnitude == 2);
//
//  uint64_t very_large_max = morton_max / 2;
//  auto fifth_points = create_points(tree_gs, 0, very_large_max, 0.001, -(tree_gs.offset[0] * 4));
//  points::converter::tree_add_points(tree_gs, tree, std::move(fifth_points));
//  require(tree.sub_trees.size() == 2);
//  require(tree.magnitude == 6);
//}
}
