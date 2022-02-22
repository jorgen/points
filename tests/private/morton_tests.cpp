#include <catch2/catch.hpp>
#include <fmt/printf.h>

#include <morton.hpp>
#include <morton_tree_coordinate_transform.hpp>


TEST_CASE("SimpleMorton", "[converter]")
{
  double half[] = {0.51200000001699664, 0.0, 0.0};
  double quarter[] = {0.256, 0.0, 0.0};
  double scale[] = {0.001, 0.001, 0.001};
  double offset[] = {0.0, 0.0, 0.0}; 

//  points::converter::morton::morton_t<uint64_t, 3> half_morton;
//  points::converter::convert_pos_to_morton(scale, offset, half, half_morton);
//  points::converter::morton::morton_t<uint64_t, 3> quarter_morton;
//  points::converter::convert_pos_to_morton(scale, offset, quarter, quarter_morton);
  //  points::converter::morton::morton64_t a;
//  a = points::converter::morton::morton_mask_create(0);
//  REQUIRE(a.data[0] == 0b111);
//  a = points::converter::morton::morton_mask_create(1);
//  REQUIRE(a.data[0] == 0b111);
//  a = points::converter::morton::morton_mask_create(2);
//  REQUIRE(a.data[0] == 0b111);
//  a = points::converter::morton::morton_mask_create(3);
//  REQUIRE(a.data[0] == 0b111111);
//  a = points::converter::morton::morton_mask_create(4);
//  REQUIRE(a.data[0] == 0b111111);
//  a = points::converter::morton::morton_mask_create(5);
//  REQUIRE(a.data[0] == 0b111111);
//  a = points::converter::morton::morton_mask_create(6);
//  REQUIRE(a.data[0] == 0b111111111);
//  a = points::converter::morton::morton_mask_create(63);
//  REQUIRE(a.data[2] == 0);
//  REQUIRE(a.data[1] == 0b11);
//  REQUIRE(a.data[0] == ~uint64_t(0));
//
//  a = points::converter::morton::morton_mask_create(127);
//  REQUIRE(a.data[2] == 1);
//  REQUIRE(a.data[1] == ~uint64_t(0));
//  REQUIRE(a.data[0] == ~uint64_t(0));
}

TEST_CASE("Morton order", "[converter]")
{
  using namespace  points::converter;
  morton::morton_t<uint32_t, 3> first = {};
  first.data[0] = 64425663;
  first.data[1] = 4959871;
  morton::morton_t<uint32_t, 3> second = {};
  second.data[0] = 2009337122;
  second.data[1] = 4959942;
  REQUIRE(first < second);
  double local_scale[3] = {0.00025, 0.00025, 0.00025};
  double local_offset[3] = {6483393,  5589339, 220};
  double world_scale[3] = {0.00025, 0.00025, 0.00025};
  double world_offset[3] = {-2097152, -2097152, -2097152};

//  morton::morton64_t world_first;
//
//  morton::morton64_t world_second;
//  double pos_first[3];
//  double pos_second[3];
//  uint64_t ipos_first1[3];
//  uint64_t ipos_second1[3];
//  convert_morton_to_pos(local_scale, local_offset, first, pos_first);
//  convert_morton_to_pos(local_scale, local_offset, second, pos_second);
//  convert_pos_to_morton(world_scale, world_offset, pos_first, world_first);
//  convert_pos_to_morton(world_scale, world_offset, pos_second, world_second);
//  REQUIRE(world_first < world_second);
}
