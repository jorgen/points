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

TEST_CASE("Morton 192 encode/decode x", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = ~uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton192_t m;
  morton::encode(pos,m);

  morton::morton192_t expected = {};
  for (int i = 0; i < 192; i++)
  {
    if (i % 3 == 0)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);
  REQUIRE(m.data[2] == expected.data[2]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  REQUIRE(outpos[0] == pos[0]);
  REQUIRE(outpos[1] == pos[1]);
  REQUIRE(outpos[2] == pos[2]);
}

TEST_CASE("Morton 192 encode/decode x top off", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = ~uint64_t(0) & (~(uint64_t(1) << 21));
  pos[1] = uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton192_t m;
  morton::encode(pos,m);

  morton::morton192_t expected = {};
  for (int i = 0; i < 192; i++)
  {
    if (i % 3 == 0 && i != 63)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);
  REQUIRE(m.data[2] == expected.data[2]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  REQUIRE(outpos[0] == pos[0]);
  REQUIRE(outpos[1] == pos[1]);
  REQUIRE(outpos[2] == pos[2]);
}

TEST_CASE("Morton 192 encode/decode y top off", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = ~uint64_t(0) & (~(uint64_t(1) << (21 + 21)));
  pos[2] = uint64_t(0);
  morton::morton192_t m;
  morton::encode(pos,m);

  morton::morton192_t expected = {};
  for (int i = 0; i < 192; i++)
  {
    if (i % 3 == 1 && i != 127)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);
  REQUIRE(m.data[2] == expected.data[2]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  REQUIRE(outpos[0] == pos[0]);
  REQUIRE(outpos[1] == pos[1]);
  REQUIRE(outpos[2] == pos[2]);
}

TEST_CASE("Morton 192 encode/decode y", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = ~uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton192_t m;
  morton::encode(pos,m);

  morton::morton192_t expected = {};
  for (int i = 0; i < 192; i++)
  {
    if (i % 3 == 1)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);
  REQUIRE(m.data[2] == expected.data[2]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  REQUIRE(outpos[0] == pos[0]);
  REQUIRE(outpos[1] == pos[1]);
  REQUIRE(outpos[2] == pos[2]);
}

TEST_CASE("Morton 192 encode_z", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = ~uint64_t(0);
  morton::morton192_t m;
  morton::encode(pos,m);

  morton::morton192_t expected = {};
  for (int i = 0; i < 192; i++)
  {
    if (i % 3 == 2)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);
  REQUIRE(m.data[2] == expected.data[2]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  REQUIRE(outpos[0] == pos[0]);
  REQUIRE(outpos[1] == pos[1]);
  REQUIRE(outpos[2] == pos[2]);
}

TEST_CASE("Morton 128 encode/decode x", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = ~uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton128_t m;
  morton::encode(pos,m);

  morton::morton128_t expected = {};
  for (int i = 0; i < 128; i++)
  {
    if (i % 3 == 0)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);

  constexpr uint64_t mask21 = (uint64_t(1) << 21) - 1;
  constexpr uint64_t mask22 = (uint64_t(1) << 22) - 1;

  uint64_t outpos[3];
  morton::decode(m, outpos);
  int xmask = 64 - (22 + 21);
  int ymask = 64 - (21 + 22);
  int zmask = 64 - (21 + 21);
  REQUIRE(outpos[0] == (pos[0] << xmask) >> xmask);
  REQUIRE(outpos[1] == (pos[1] << ymask) >> ymask);
  REQUIRE(outpos[2] == (pos[2] << zmask) >> zmask);
}

TEST_CASE("Morton 128 encode/decode y", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = ~uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton128_t m;
  morton::encode(pos,m);

  morton::morton128_t expected = {};
  for (int i = 0; i < 128; i++)
  {
    if (i % 3 == 1)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  int xmask = 64 - (22 + 21);
  int ymask = 64 - (21 + 22);
  int zmask = 64 - (21 + 21);
  REQUIRE(outpos[0] == (pos[0] << xmask) >> xmask);
  REQUIRE(outpos[1] == (pos[1] << ymask) >> ymask);
  REQUIRE(outpos[2] == (pos[2] << zmask) >> zmask);
}

TEST_CASE("Morton 128 encode/decode z", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = ~uint64_t(0);
  morton::morton128_t m;
  morton::encode(pos,m);

  morton::morton128_t expected = {};
  for (int i = 0; i < 128; i++)
  {
    if (i % 3 == 2)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);

  constexpr uint64_t mask21 = (uint64_t(1) << 21) - 1;
  constexpr uint64_t mask22 = (uint64_t(1) << 22) - 1;

  uint64_t outpos[3];
  morton::decode(m, outpos);
  int xmask = 64 - (22 + 21);
  int ymask = 64 - (21 + 22);
  int zmask = 64 - (21 + 21);
  REQUIRE(outpos[0] == (pos[0] << xmask) >> xmask);
  REQUIRE(outpos[1] == (pos[1] << ymask) >> ymask);
  REQUIRE(outpos[2] == (pos[2] << zmask) >> zmask);
}

TEST_CASE("Morton 64 encode/decode x", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = ~uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton64_t m;
  morton::encode(pos,m);

  morton::morton64_t expected = {};
  for (int i = 0; i < 64; i++)
  {
    if (i % 3 == 0)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  constexpr uint32_t mask21 = (uint32_t(1) << 21) - 1;
  constexpr uint32_t mask22 = (uint32_t(1) << 22) - 1;
  uint64_t expected_output[3];
  expected_output[0] = (pos[0] & mask22);
  expected_output[1] = (pos[1] & mask21);
  expected_output[2] = (pos[2] & mask21);

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 64 encode/decode y", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = ~uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton64_t m;
  morton::encode(pos,m);

  morton::morton64_t expected = {};
  for (int i = 0; i < 64; i++)
  {
    if (i % 3 == 1)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  constexpr uint32_t mask21 = (uint32_t(1) << 21) - 1;
  constexpr uint32_t mask22 = (uint32_t(1) << 22) - 1;
  uint64_t expected_output[3];
  expected_output[0] = (pos[0] & mask22);
  expected_output[1] = (pos[1] & mask21);
  expected_output[2] = (pos[2] & mask21);

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 64 encode/decode z", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = ~uint64_t(0);
  morton::morton64_t m;
  morton::encode(pos,m);

  morton::morton64_t expected = {};
  for (int i = 0; i < 64; i++)
  {
    if (i % 3 == 2)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  constexpr uint32_t mask21 = (uint32_t(1) << 21) - 1;
  constexpr uint32_t mask22 = (uint32_t(1) << 22) - 1;
  uint64_t expected_output[3];
  expected_output[0] = (pos[0] & mask22);
  expected_output[1] = (pos[1] & mask21);
  expected_output[2] = (pos[2] & mask21);

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 32 encode/decode x", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = ~uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton32_t m;
  morton::encode(pos,m);

  morton::morton32_t expected = {};
  for (int i = 0; i < 32; i++)
  {
    if (i % 3 == 0)
      expected.data[i / 32] |= uint32_t(1) << (i % 32);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  uint64_t expected_output[3];
  constexpr uint64_t mask11 = (uint64_t(1) << 11) - 1;
  constexpr uint64_t mask10 = (uint64_t(1) << 10) - 1;
  expected_output[0] = pos[0] & mask11;
  expected_output[1] = pos[1] & mask11;
  expected_output[2] = pos[2] & mask10;

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 32 encode/decode y", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = ~uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton32_t m;
  morton::encode(pos,m);

  morton::morton32_t expected = {};
  for (int i = 0; i < 32; i++)
  {
    if (i % 3 == 1)
      expected.data[i / 32] |= uint32_t(1) << (i % 32);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  uint64_t expected_output[3];
  constexpr uint64_t mask11 = (uint64_t(1) << 11) - 1;
  constexpr uint64_t mask10 = (uint64_t(1) << 10) - 1;
  expected_output[0] = pos[0] & mask11;
  expected_output[1] = pos[1] & mask11;
  expected_output[2] = pos[2] & mask10;

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 32 encode/decode z", "[converter]")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = ~uint64_t(0);
  morton::morton32_t m;
  morton::encode(pos,m);

  morton::morton32_t expected = {};
  for (int i = 0; i < 32; i++)
  {
    if (i % 3 == 2)
      expected.data[i / 32] |= uint32_t(1) << (i % 32);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  uint64_t expected_output[3];
  constexpr uint64_t mask11 = (uint64_t(1) << 11) - 1;
  constexpr uint64_t mask10 = (uint64_t(1) << 10) - 1;
  expected_output[0] = pos[0] & mask11;
  expected_output[1] = pos[1] & mask11;
  expected_output[2] = pos[2] & mask10;

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton downcast", "[converter]")
{
  using namespace points::converter;
  morton::morton192_t m192;
  m192.data[0] = ~uint64_t(0);
  m192.data[1] = ~uint64_t(0);
  m192.data[2] = ~uint64_t(0);

  morton::morton128_t m128;
  morton::morton_downcast(m192, m128);
  REQUIRE(m128.data[0] == ~uint64_t(0));
  REQUIRE(m128.data[1] == (~uint64_t(0)) >> 2);

  morton::morton64_t m64;
  morton::morton_downcast(m128, m64);
  REQUIRE(m64.data[0] == (~uint64_t(0) >> 1));

  morton::morton32_t m32;
  morton::morton_downcast(m64, m32);
  REQUIRE(m32.data[0] == (~uint32_t(0)) >> 2);
}
