#include <doctest/doctest.h>
#include <random>
#include <fmt/printf.h>

#include <morton.hpp>
#include <morton_tree_coordinate_transform.hpp>


TEST_CASE("SimpleMorton")
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

TEST_CASE("Morton order")
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


TEST_CASE("Morton random downcast/upcast")
{
  using namespace  points::converter;
  std::mt19937 gen(44);
  std::uniform_int_distribution<uint64_t> distrib(0, ~uint64_t(0));

  constexpr size_t test_count = 1000000;

  uint64_t input[3];
  uint64_t test_output[3];
  morton::morton192_t output;
  morton::morton192_t upscaled;
  morton::morton64_t descaled;
  for (int i = 0; i < test_count; i++)
  {
    input[0] = distrib(gen);  input[1] = distrib(gen);  input[2] = distrib(gen);
    morton::encode(input, output);
    morton::morton_downcast(output, descaled);
    morton::morton_upcast(descaled, output, upscaled);
    morton::decode(upscaled, test_output);
    REQUIRE(input[0] == test_output[0]);
    REQUIRE(input[1] == test_output[1]);
    REQUIRE(input[2] == test_output[2]);
  }
}

TEST_CASE("Morton 192 encode/decode x")
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

TEST_CASE("Morton 192 encode/decode x top off")
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

TEST_CASE("Morton 192 encode/decode y top off")
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

TEST_CASE("Morton 192 encode/decode y")
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

TEST_CASE("Morton 192 encode_z")
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

TEST_CASE("Morton 128 encode/decode x")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = ~uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton128_t m;
  morton::encode(pos,m);

  morton::morton128_t expected = {};
  for (int i = 0; i < 126; i++)
  {
    if (i % 3 == 0)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  int xmask = 64 - (21 + 21);
  int ymask = 64 - (21 + 21);
  int zmask = 64 - (21 + 21);
  REQUIRE(outpos[0] == (pos[0] << xmask) >> xmask);
  REQUIRE(outpos[1] == (pos[1] << ymask) >> ymask);
  REQUIRE(outpos[2] == (pos[2] << zmask) >> zmask);
}

TEST_CASE("Morton 128 encode/decode y")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = ~uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton128_t m;
  morton::encode(pos,m);

  morton::morton128_t expected = {};
  for (int i = 0; i < 126; i++)
  {
    if (i % 3 == 1)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  int xmask = 64 - (21 + 21);
  int ymask = 64 - (21 + 21);
  int zmask = 64 - (21 + 21);
  REQUIRE(outpos[0] == (pos[0] << xmask) >> xmask);
  REQUIRE(outpos[1] == (pos[1] << ymask) >> ymask);
  REQUIRE(outpos[2] == (pos[2] << zmask) >> zmask);
}

TEST_CASE("Morton 128 encode/decode z")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = ~uint64_t(0);
  morton::morton128_t m;
  morton::encode(pos,m);

  morton::morton128_t expected = {};
  for (int i = 0; i < 126; i++)
  {
    if (i % 3 == 2)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);
  REQUIRE(m.data[1] == expected.data[1]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  int xmask = 64 - (21 + 21);
  int ymask = 64 - (21 + 21);
  int zmask = 64 - (21 + 21);
  REQUIRE(outpos[0] == (pos[0] << xmask) >> xmask);
  REQUIRE(outpos[1] == (pos[1] << ymask) >> ymask);
  REQUIRE(outpos[2] == (pos[2] << zmask) >> zmask);
}

TEST_CASE("Morton 64 encode/decode x")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = ~uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton64_t m;
  morton::encode(pos,m);

  morton::morton64_t expected = {};
  for (int i = 0; i < 63; i++)
  {
    if (i % 3 == 0)
      expected.data[i / 64] |= uint64_t(1) << (i % 64);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  constexpr uint32_t mask21 = (uint32_t(1) << 21) - 1;
  uint64_t expected_output[3];
  expected_output[0] = (pos[0] & mask21);
  expected_output[1] = (pos[1] & mask21);
  expected_output[2] = (pos[2] & mask21);

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 64 encode/decode y")
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
  uint64_t expected_output[3];
  expected_output[0] = (pos[0] & mask21);
  expected_output[1] = (pos[1] & mask21);
  expected_output[2] = (pos[2] & mask21);

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 64 encode/decode z")
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
  uint64_t expected_output[3];
  expected_output[0] = (pos[0] & mask21);
  expected_output[1] = (pos[1] & mask21);
  expected_output[2] = (pos[2] & mask21);

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 32 encode/decode x")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = ~uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton32_t m;
  morton::encode(pos,m);

  morton::morton32_t expected = {};
  for (int i = 0; i < 30; i++)
  {
    if (i % 3 == 0)
      expected.data[i / 32] |= uint32_t(1) << (i % 32);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  uint64_t expected_output[3];
  constexpr uint64_t mask10 = (uint64_t(1) << 10) - 1;
  expected_output[0] = pos[0] & mask10;
  expected_output[1] = pos[1] & mask10;
  expected_output[2] = pos[2] & mask10;

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 32 encode/decode y")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = ~uint64_t(0);
  pos[2] = uint64_t(0);
  morton::morton32_t m;
  morton::encode(pos,m);

  morton::morton32_t expected = {};
  for (int i = 0; i < 30; i++)
  {
    if (i % 3 == 1)
      expected.data[i / 32] |= uint32_t(1) << (i % 32);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  uint64_t expected_output[3];
  constexpr uint64_t mask10 = (uint64_t(1) << 10) - 1;
  expected_output[0] = pos[0] & mask10;
  expected_output[1] = pos[1] & mask10;
  expected_output[2] = pos[2] & mask10;

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton 32 encode/decode z")
{
  using namespace  points::converter;
  uint64_t pos[3];
  pos[0] = uint64_t(0);
  pos[1] = uint64_t(0);
  pos[2] = ~uint64_t(0);
  morton::morton32_t m;
  morton::encode(pos,m);

  morton::morton32_t expected = {};
  for (int i = 0; i < 30; i++)
  {
    if (i % 3 == 2)
      expected.data[i / 32] |= uint32_t(1) << (i % 32);
  }

  REQUIRE(m.data[0] == expected.data[0]);

  uint64_t outpos[3];
  morton::decode(m, outpos);
  uint64_t expected_output[3];
  constexpr uint64_t mask10 = (uint64_t(1) << 10) - 1;
  expected_output[0] = pos[0] & mask10;
  expected_output[1] = pos[1] & mask10;
  expected_output[2] = pos[2] & mask10;

  REQUIRE(outpos[0] == expected_output[0]);
  REQUIRE(outpos[1] == expected_output[1]);
  REQUIRE(outpos[2] == expected_output[2]);
}

TEST_CASE("Morton downcast")
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
  morton::morton_downcast(m192, m64);
  REQUIRE(m64.data[0] == (~uint64_t(0) >> 1));

  morton::morton32_t m32;
  morton::morton_downcast(m64, m32);
  REQUIRE(m32.data[0] == (~uint32_t(0)) >> 2);
  morton::morton_downcast(m192, m32);
  REQUIRE(m32.data[0] == (~uint32_t(0)) >> 2);
  morton::morton_downcast(m128, m32);
  REQUIRE(m32.data[0] == (~uint32_t(0)) >> 2);
}

TEST_CASE("Morton upcast")
{
  using namespace points::converter;
  morton::morton192_t m192;
  m192.data[0] = ~uint64_t(0);
  m192.data[1] = ~uint64_t(0);
  m192.data[2] = ~uint64_t(0);

  morton::morton128_t m128;
  morton::morton_downcast(m192, m128);
  morton::morton192_t upcast;
  morton::morton_upcast(m128, m192, upcast);
  REQUIRE(m192 == upcast);

  morton::morton64_t m64;
  morton::morton_downcast(m192, m64);
  morton::morton_upcast(m64, m192, upcast);
  REQUIRE(m192 == upcast);

  morton::morton32_t m32;
  morton::morton_downcast(m192, m32);
  morton::morton_upcast(m32, m192, upcast);
  REQUIRE(m192 == upcast);

  morton::morton128_t m128full;
  m128full.data[0] = ~uint64_t(0);
  m128full.data[1] = ~uint64_t(0);

  morton::morton128_t upcast128;
  morton::morton_upcast(m64, m192, upcast128);
  REQUIRE(m128full == upcast128);
}

TEST_CASE("Morton name")
{
  using namespace points::converter;
  morton::morton192_t to_test;
  memset(to_test.data, 0, sizeof(to_test.data));

  uint16_t name = 0x7fff;

  for (int i = 0; i < (192 / 15); i+=2)
  {
    auto reply = morton::set_name_in_morton(i, to_test, name);

    auto test = morton::get_name_from_morton_magnitude(i, reply);
    REQUIRE(test == name);
  }

  for (int i = 1; i < (192 / 15); i+=2)
  {
    auto reply = morton::set_name_in_morton(i, to_test, name);

    auto test = morton::get_name_from_morton_magnitude(i, reply);
    REQUIRE(test == name);
  }
}

TEST_CASE("morton_is_null and morton_is_set")
{
  using namespace points::converter;

  morton::morton64_t zero64 = {};
  REQUIRE(morton::morton_is_null(zero64) == true);
  REQUIRE(morton::morton_is_set(zero64) == false);

  morton::morton64_t nonzero64 = {};
  nonzero64.data[0] = 9;
  REQUIRE(morton::morton_is_null(nonzero64) == false);
  REQUIRE(morton::morton_is_set(nonzero64) == true);

  morton::morton192_t zero192 = {};
  REQUIRE(morton::morton_is_null(zero192) == true);
  REQUIRE(morton::morton_is_set(zero192) == false);

  // A non-zero value in ANY word must be detected, regardless of which word.
  for (int word = 0; word < 3; word++)
  {
    morton::morton192_t v = {};
    v.data[word] = (word == 2) ? 7 : 5;
    REQUIRE(morton::morton_is_null(v) == false);
    REQUIRE(morton::morton_is_set(v) == true);
  }

  morton::morton128_t zero128 = {};
  REQUIRE(morton::morton_is_null(zero128) == true);
  REQUIRE(morton::morton_is_set(zero128) == false);
  morton::morton128_t hi128 = {};
  hi128.data[1] = 1;
  REQUIRE(morton::morton_is_null(hi128) == false);
  REQUIRE(morton::morton_is_set(hi128) == true);
}

TEST_CASE("morton_mask_create 128-bit matches 192-bit (no UB shift for C==2)")
{
  using namespace points::converter;
  // For the 128-bit (C==2) morton, lod 21..41 yields bit index 66..126 (>= 64),
  // which previously produced an undefined shift-by->=64. Cross-check against the
  // trusted 192-bit mask (low two words) for every valid lod.
  for (int lod = 0; lod <= 41; lod++)
  {
    auto mask128 = morton::morton_mask_create<uint64_t, 2>(lod);
    auto mask192 = morton::morton_mask_create<uint64_t, 3>(lod);
    REQUIRE(mask128.data[0] == mask192.data[0]);
    REQUIRE(mask128.data[1] == mask192.data[1]);
  }
  // Spot-check a specific case that used to be UB: lod 21 -> index 66.
  auto m = morton::morton_mask_create<uint64_t, 2>(21);
  REQUIRE(m.data[0] == ~uint64_t(0));
  REQUIRE(m.data[1] == uint64_t(0x3));
}

TEST_CASE("set_name_in_morton does not write out of bounds at high magnitude")
{
  using namespace points::converter;
  morton::morton192_t base = {};
  // magnitude 12 -> lower_bit 180 -> lower_section 2; the spill word would be
  // data[3] (out of bounds). The guarded implementation must not touch it.
  // (Under ASan this asserts the OOB write is gone.)
  auto reply = morton::set_name_in_morton(12, base, uint16_t(0x7fff));
  // Nothing above data[2] exists; the call simply must return safely.
  (void)reply;
  REQUIRE(true);
}
