#include <catch2/catch.hpp>
#include <fmt/printf.h>

#include <tree_p.h>
#include <input_header_p.h>
#include <points/converter/default_attribute_names.h>

namespace {

static points::converter::points_t create_points(uint64_t min, uint64_t max, int data_offset)
{
  points::converter::points_t points;
  points.header.morton_min.data[0] = min;
  points.header.morton_min.data[1] = 0;
  points.header.morton_min.data[2] = 0;
  points.header.morton_max.data[0] = max;
  points.header.morton_max.data[1] = 0;
  points.header.morton_max.data[2] = 0;
  points.header.point_count = 256;
  points.header.scale[0] = 0.2;
  points.header.scale[1] = 0.2;
  points.header.scale[2] = 0.2;
  points.header.offset[0] = 0.0;
  points.header.offset[1] = 0.0;
  points.header.offset[2] = 0.0;
  points::converter::morton::decode(points.header.morton_min, points.header.scale, points.header.min);
  points::converter::morton::decode(points.header.morton_max, points.header.scale, points.header.max);
  points::converter::header_add_attribute(&points.header, POINTS_ATTRIBUTE_XYZ, sizeof(POINTS_ATTRIBUTE_XYZ), points::converter::format_i32, points::converter::components_3);
  points::converter::header_p_calculate_morton_aabb(points.header);

  points.buffers.data.emplace_back(new uint8_t[256 * 3 * 4]);
  auto databuffer = points.buffers.data.back().get();
  uint32_t *databuffer_uint32_t = reinterpret_cast<uint32_t *>(databuffer);
  points.buffers.buffers.emplace_back();
  points.buffers.buffers.back().data = databuffer;
  points.buffers.buffers.back().size = 256 * 3 * 4;
  uint64_t step_size = std::max((max - min) / 256, uint64_t(1));
  for (int i = 0; i < 256; i++)
  {
    databuffer_uint32_t[i * 3 + 0] = min + data_offset + i;
    databuffer_uint32_t[i * 3 + 1] = 0;
    databuffer_uint32_t[i * 3 + 2] = 0;
  }
  return points;
}
  
static points::converter::tree_global_state_t create_tree_global_state(const points::converter::points_t &points, int node_limit)
{
  points::converter::tree_global_state_t ret;
  ret.node_limit = node_limit;
  memcpy(ret.tree_scale, points.header.scale, sizeof(ret.tree_scale));
  return ret;
}


TEST_CASE("Initialize Empty tree", "[converter, tree_t]")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  auto points = create_points(0, morton_max, 0);
  void *buffer_ptr = points.buffers.buffers.back().data;
  
  auto tree_gs = create_tree_global_state(points, 1000);
  points::converter::tree_t tree;
  points::converter::tree_initialize(tree_gs, tree, std::move(points));
  REQUIRE(tree.morton_max.data[0] == morton_max);
  REQUIRE(tree.morton_max.data[1] == 0);
  REQUIRE(tree.morton_max.data[2] == 0);

  REQUIRE(tree.morton_min.data[0] == 0);
  REQUIRE(tree.morton_min.data[1] == 0);
  REQUIRE(tree.morton_min.data[2] == 0);

  REQUIRE(tree.data[0].back().data.back().buffers.data.back().get() == buffer_ptr);
}

TEST_CASE("Add inclusion", "[converter, tree_t]")
{
  uint64_t morton_max = ((uint64_t(1) << (1 * 3 * 5)) - 1);
  auto points = create_points(0, morton_max, 0);
  auto tree_gs = create_tree_global_state(points, 1000);
  points::converter::tree_t tree;
  points::converter::tree_initialize(tree_gs, tree, std::move(points));

  auto second_points = create_points(0, morton_max, 256);
  void *buffer_ptr = second_points.buffers.buffers.back().data;

  points::converter::tree_add_points(tree_gs, tree, std::move(second_points));

  REQUIRE(tree.morton_max.data[0] == morton_max);
  REQUIRE(tree.morton_max.data[1] == 0);
  REQUIRE(tree.morton_max.data[2] == 0);

  REQUIRE(tree.morton_min.data[0] == 0);
  REQUIRE(tree.morton_min.data[1] == 0);
  REQUIRE(tree.morton_min.data[2] == 0);

  REQUIRE(tree.data[0][0].data.size() == 2);
  REQUIRE(tree.data[0][0].data.back().buffers.data.back().get() == buffer_ptr);
  REQUIRE(tree.nodes[0][0] == 0);
}

TEST_CASE("Add_new_node", "[converter, tree_t]")
{
  uint64_t morton_max = ((uint64_t(1) << (3 * 3)) - 1);
  auto points = create_points(0, morton_max, 0);
  auto tree_gs = create_tree_global_state(points, 256);
  points::converter::tree_t tree;
  points::converter::tree_initialize(tree_gs, tree, std::move(points));

  auto second_points = create_points(0, morton_max, 256);
  void *buffer_ptr = second_points.buffers.buffers.back().data;
  points::converter::tree_add_points(tree_gs, tree, std::move(second_points));

  REQUIRE(tree.nodes[0][0] != 0);
  REQUIRE(tree.data[0][0].data.size() == 0);
}

//TEST_CASE("Add new_subtree", "[converter, tree_t]")
//{
//}
}
