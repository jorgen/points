#include <catch2/catch.hpp>
#include <fmt/printf.h>

#include <morton_p.h>


TEST_CASE("SimpleMorton", "[converter]")
{
  double half[] = {0.51200000001699664, 0.0, 0.0};
  double quarter[] = {0.256, 0.0, 0.0};
  double scale[] = {0.001, 0.001, 0.001};
  
  points::converter::morton::morton_t<uint32_t> half_morton;
  points::converter::morton::encode(half, scale, half_morton);
  points::converter::morton::morton_t<uint32_t> quarter_morton;
  points::converter::morton::encode(quarter, scale, quarter_morton);
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
