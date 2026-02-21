#include <catch2/catch.hpp>
#include <byte_shuffle.hpp>

#include <cstring>
#include <numeric>
#include <vector>

using namespace points::converter;

TEST_CASE("byte_shuffle round trip single byte type", "[converter]")
{
  std::vector<uint8_t> src = {1, 2, 3, 4, 5};
  std::vector<uint8_t> shuffled(src.size());
  std::vector<uint8_t> unshuffled(src.size());

  byte_shuffle(src.data(), shuffled.data(), uint32_t(src.size()), 1, 1);
  byte_unshuffle(shuffled.data(), unshuffled.data(), uint32_t(src.size()), 1, 1);

  REQUIRE(memcmp(src.data(), shuffled.data(), src.size()) == 0);
  REQUIRE(memcmp(src.data(), unshuffled.data(), src.size()) == 0);
}

TEST_CASE("byte_shuffle round trip u16 single component", "[converter]")
{
  uint16_t values[] = {0x0102, 0x0304, 0x0506};
  uint32_t total = sizeof(values);
  std::vector<uint8_t> shuffled(total);
  std::vector<uint8_t> unshuffled(total);

  byte_shuffle(reinterpret_cast<uint8_t *>(values), shuffled.data(), total, 2, 1);
  byte_unshuffle(shuffled.data(), unshuffled.data(), total, 2, 1);

  REQUIRE(memcmp(values, unshuffled.data(), total) == 0);
}

TEST_CASE("byte_shuffle round trip u32 xyz components", "[converter]")
{
  uint32_t values[] = {0x11223344, 0x55667788, 0x99AABBCC,
                       0xDDEEFF00, 0x12345678, 0x9ABCDEF0};
  uint32_t total = sizeof(values);
  std::vector<uint8_t> shuffled(total);
  std::vector<uint8_t> unshuffled(total);

  byte_shuffle(reinterpret_cast<uint8_t *>(values), shuffled.data(), total, 4, 3);
  byte_unshuffle(shuffled.data(), unshuffled.data(), total, 4, 3);

  REQUIRE(memcmp(values, unshuffled.data(), total) == 0);
}

TEST_CASE("byte_shuffle round trip r64 xyz components", "[converter]")
{
  double values[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
  uint32_t total = sizeof(values);
  std::vector<uint8_t> shuffled(total);
  std::vector<uint8_t> unshuffled(total);

  byte_shuffle(reinterpret_cast<uint8_t *>(values), shuffled.data(), total, 8, 3);
  byte_unshuffle(shuffled.data(), unshuffled.data(), total, 8, 3);

  REQUIRE(memcmp(values, unshuffled.data(), total) == 0);
}

TEST_CASE("byte_shuffle round trip m192 single component", "[converter]")
{
  uint8_t values[24 * 3];
  for (uint32_t i = 0; i < sizeof(values); i++)
    values[i] = static_cast<uint8_t>(i);

  uint32_t total = sizeof(values);
  std::vector<uint8_t> shuffled(total);
  std::vector<uint8_t> unshuffled(total);

  byte_shuffle(values, shuffled.data(), total, 24, 1);
  byte_unshuffle(shuffled.data(), unshuffled.data(), total, 24, 1);

  REQUIRE(memcmp(values, unshuffled.data(), total) == 0);
}

TEST_CASE("byte_shuffle known layout u32 single component", "[converter]")
{
  // 3 elements, typesize=4, components=1 => stride=4, 4 bands of 3 bytes each
  uint32_t values[] = {0x04030201, 0x08070605, 0x0C0B0A09};
  uint32_t total = sizeof(values);
  std::vector<uint8_t> shuffled(total);

  byte_shuffle(reinterpret_cast<uint8_t *>(values), shuffled.data(), total, 4, 1);

  auto src = reinterpret_cast<const uint8_t *>(values);
  // Band 0 (byte 0 of each element): src[0], src[4], src[8]
  REQUIRE(shuffled[0] == src[0]);
  REQUIRE(shuffled[1] == src[4]);
  REQUIRE(shuffled[2] == src[8]);
  // Band 1 (byte 1 of each element): src[1], src[5], src[9]
  REQUIRE(shuffled[3] == src[1]);
  REQUIRE(shuffled[4] == src[5]);
  REQUIRE(shuffled[5] == src[9]);
  // Band 2 (byte 2 of each element): src[2], src[6], src[10]
  REQUIRE(shuffled[6] == src[2]);
  REQUIRE(shuffled[7] == src[6]);
  REQUIRE(shuffled[8] == src[10]);
  // Band 3 (byte 3 of each element): src[3], src[7], src[11]
  REQUIRE(shuffled[9] == src[3]);
  REQUIRE(shuffled[10] == src[7]);
  REQUIRE(shuffled[11] == src[11]);
}

TEST_CASE("byte_shuffle known layout u8 xyz components", "[converter]")
{
  // typesize=1, components=3 => stride=3, 3 bands of element_count bytes
  // 2 elements: {x0,y0,z0, x1,y1,z1}
  uint8_t src[] = {10, 20, 30, 40, 50, 60};
  uint32_t total = sizeof(src);
  std::vector<uint8_t> shuffled(total);

  byte_shuffle(src, shuffled.data(), total, 1, 3);

  // Band 0 (component 0 byte 0): x0, x1
  REQUIRE(shuffled[0] == 10);
  REQUIRE(shuffled[1] == 40);
  // Band 1 (component 1 byte 0): y0, y1
  REQUIRE(shuffled[2] == 20);
  REQUIRE(shuffled[3] == 50);
  // Band 2 (component 2 byte 0): z0, z1
  REQUIRE(shuffled[4] == 30);
  REQUIRE(shuffled[5] == 60);
}

TEST_CASE("byte_shuffle handles remainder bytes", "[converter]")
{
  // 7 bytes with stride=4 (typesize=2, components=2) => 1 element + 3 remainder bytes
  uint8_t src[] = {1, 2, 3, 4, 0xAA, 0xBB, 0xCC};
  uint32_t total = sizeof(src);
  std::vector<uint8_t> shuffled(total);
  std::vector<uint8_t> unshuffled(total);

  byte_shuffle(src, shuffled.data(), total, 2, 2);
  byte_unshuffle(shuffled.data(), unshuffled.data(), total, 2, 2);

  REQUIRE(memcmp(src, unshuffled.data(), total) == 0);
  // Remainder bytes should be copied verbatim
  REQUIRE(shuffled[4] == 0xAA);
  REQUIRE(shuffled[5] == 0xBB);
  REQUIRE(shuffled[6] == 0xCC);
}

TEST_CASE("byte_shuffle handles empty buffer", "[converter]")
{
  uint8_t dummy = 0;
  std::vector<uint8_t> shuffled(1);
  std::vector<uint8_t> unshuffled(1);

  byte_shuffle(&dummy, shuffled.data(), 0, 4, 3);
  byte_unshuffle(&dummy, unshuffled.data(), 0, 4, 3);
  // No crash is the test
}

TEST_CASE("byte_shuffle round trip matrix", "[converter]")
{
  uint32_t typesizes[] = {1, 2, 4, 8, 16, 24};
  uint32_t comp_counts[] = {1, 2, 3, 4};

  for (auto typesize : typesizes)
  {
    for (auto components : comp_counts)
    {
      SECTION("typesize=" + std::to_string(typesize) + " components=" + std::to_string(components))
      {
        uint32_t stride = typesize * components;
        uint32_t element_count = 100;
        uint32_t total = stride * element_count;

        std::vector<uint8_t> src(total);
        for (uint32_t i = 0; i < total; i++)
          src[i] = static_cast<uint8_t>((i * 37 + 13) & 0xFF);

        std::vector<uint8_t> shuffled(total);
        std::vector<uint8_t> unshuffled(total);

        byte_shuffle(src.data(), shuffled.data(), total, typesize, components);
        byte_unshuffle(shuffled.data(), unshuffled.data(), total, typesize, components);

        REQUIRE(memcmp(src.data(), unshuffled.data(), total) == 0);
      }
    }
  }
}
