#include <doctest/doctest.h>
#include <compression_preprocess.hpp>
#include <byte_shuffle.hpp>

#include <cstring>
#include <vector>

using namespace points::converter;

TEST_CASE("popcount32 known values")
{
  REQUIRE(popcount32(0) == 0);
  REQUIRE(popcount32(1) == 1);
  REQUIRE(popcount32(0xFFFFFFFF) == 32);
  REQUIRE(popcount32(0xAAAAAAAA) == 16);
  REQUIRE(popcount32(0x55555555) == 16);
  REQUIRE(popcount32(0x80000000) == 1);
  REQUIRE(popcount32(0x0F0F0F0F) == 16);
}

TEST_CASE("delta_encode u32 sorted correct deltas")
{
  uint32_t values[] = {10, 20, 35, 100, 200};
  uint32_t size = sizeof(values);
  auto data = reinterpret_cast<uint8_t *>(values);

  bool result = delta_encode_morton(data, size, 4);
  REQUIRE(result == true);

  REQUIRE(values[0] == 10);
  REQUIRE(values[1] == 10);
  REQUIRE(values[2] == 15);
  REQUIRE(values[3] == 65);
  REQUIRE(values[4] == 100);
}

TEST_CASE("delta_encode u32 unsorted returns false")
{
  uint32_t values[] = {10, 5, 20, 30};
  uint32_t original[4];
  memcpy(original, values, sizeof(values));

  bool result = delta_encode_morton(reinterpret_cast<uint8_t *>(values), sizeof(values), 4);
  REQUIRE(result == false);
  REQUIRE(memcmp(values, original, sizeof(values)) == 0);
}

TEST_CASE("delta_encode_decode round trip u32")
{
  uint32_t values[] = {0, 5, 10, 100, 1000, 50000, 0xFFFFFFFF};
  uint32_t original[7];
  memcpy(original, values, sizeof(values));

  bool encoded = delta_encode_morton(reinterpret_cast<uint8_t *>(values), sizeof(values), 4);
  REQUIRE(encoded == true);

  delta_decode_morton(reinterpret_cast<uint8_t *>(values), sizeof(values), 4);
  REQUIRE(memcmp(values, original, sizeof(values)) == 0);
}

TEST_CASE("delta_encode_decode round trip u64")
{
  uint64_t values[] = {0, 100, 1000, 1000000, 0xFFFFFFFFFFFFFFFF};
  uint64_t original[5];
  memcpy(original, values, sizeof(values));

  bool encoded = delta_encode_morton(reinterpret_cast<uint8_t *>(values), sizeof(values), 8);
  REQUIRE(encoded == true);

  delta_decode_morton(reinterpret_cast<uint8_t *>(values), sizeof(values), 8);
  REQUIRE(memcmp(values, original, sizeof(values)) == 0);
}

TEST_CASE("delta_encode_decode round trip u128")
{
  // 3 elements of 16 bytes each, sorted by MSW-first comparison
  uint8_t data[48];
  memset(data, 0, sizeof(data));

  // Element 0: {LSW=0, MSW=0}
  // Element 1: {LSW=0xFFFFFFFFFFFFFFFF, MSW=0} — tests carry across 64-bit boundary
  uint64_t e1_lo = 0xFFFFFFFFFFFFFFFF;
  memcpy(data + 16, &e1_lo, 8);
  // Element 2: {LSW=0, MSW=1}
  uint64_t e2_hi = 1;
  memcpy(data + 32 + 8, &e2_hi, 8);

  uint8_t original[48];
  memcpy(original, data, sizeof(data));

  bool encoded = delta_encode_morton(data, sizeof(data), 16);
  REQUIRE(encoded == true);

  delta_decode_morton(data, sizeof(data), 16);
  REQUIRE(memcmp(data, original, sizeof(data)) == 0);
}

TEST_CASE("delta_encode_decode round trip u192")
{
  // 3 elements of 24 bytes each, sorted
  uint8_t data[72];
  memset(data, 0, sizeof(data));

  // Element 0: all zeros
  // Element 1: LSW=1, rest=0
  uint64_t one = 1;
  memcpy(data + 24, &one, 8);
  // Element 2: LSW=0, MSW_mid=0, MSW_hi=1  (big jump across multiple words)
  uint64_t hi = 1;
  memcpy(data + 48 + 16, &hi, 8);

  uint8_t original[72];
  memcpy(original, data, sizeof(data));

  bool encoded = delta_encode_morton(data, sizeof(data), 24);
  REQUIRE(encoded == true);

  delta_decode_morton(data, sizeof(data), 24);
  REQUIRE(memcmp(data, original, sizeof(data)) == 0);
}

TEST_CASE("delta_encode constant values produces zeros")
{
  uint32_t values[] = {42, 42, 42, 42, 42};
  bool encoded = delta_encode_morton(reinterpret_cast<uint8_t *>(values), sizeof(values), 4);
  REQUIRE(encoded == true);

  REQUIRE(values[0] == 42);
  for (int i = 1; i < 5; i++)
    REQUIRE(values[i] == 0);
}

TEST_CASE("delta_encode single element returns true")
{
  uint32_t value = 12345;
  bool result = delta_encode_morton(reinterpret_cast<uint8_t *>(&value), sizeof(value), 4);
  REQUIRE(result == true);
  REQUIRE(value == 12345);
}

TEST_CASE("delta_encode too small buffer returns false")
{
  uint8_t data[3] = {1, 2, 3};
  bool result = delta_encode_morton(data, 3, 4);
  REQUIRE(result == false);
}

TEST_CASE("delta_encode unsupported type_size returns false")
{
  uint8_t data[9] = {};
  bool result = delta_encode_morton(data, 9, 3);
  REQUIRE(result == false);
}

TEST_CASE("detect_constant_bands all constant")
{
  // 4 elements, typesize=2, components=1 => stride=2, 2 bands of 4 bytes
  // Make data where every element has the same value
  uint16_t values[] = {0x1234, 0x1234, 0x1234, 0x1234};
  uint32_t total = sizeof(values);

  // Byte-shuffle first
  std::vector<uint8_t> shuffled(total);
  byte_shuffle(reinterpret_cast<uint8_t *>(values), shuffled.data(), total, 2, 1);

  auto result = detect_constant_bands(shuffled.data(), total, 2, 1);

  REQUIRE(result.band_mask != 0);
  REQUIRE(popcount32(result.band_mask) == 2);
  REQUIRE(result.constant_values.size() == 2);
  REQUIRE(result.compacted_data.empty());
}

TEST_CASE("detect_constant_bands no constant")
{
  // 3 elements, typesize=2, components=1 => stride=2, 2 bands of 3 bytes
  uint16_t values[] = {0x0102, 0x0304, 0x0506};
  uint32_t total = sizeof(values);

  std::vector<uint8_t> shuffled(total);
  byte_shuffle(reinterpret_cast<uint8_t *>(values), shuffled.data(), total, 2, 1);

  auto result = detect_constant_bands(shuffled.data(), total, 2, 1);

  REQUIRE(result.band_mask == 0);
  REQUIRE(result.constant_values.empty());
  REQUIRE(result.compacted_data.size() == total);
  REQUIRE(memcmp(result.compacted_data.data(), shuffled.data(), total) == 0);
}

TEST_CASE("detect_constant_bands mixed")
{
  // 3 elements of u8 x 3 components => stride=3, 3 bands of 3 bytes
  // Component 0 varies, component 1 constant (0x42), component 2 varies
  uint8_t src[] = {10, 0x42, 30,
                   20, 0x42, 60,
                   30, 0x42, 90};
  uint32_t total = sizeof(src);

  std::vector<uint8_t> shuffled(total);
  byte_shuffle(src, shuffled.data(), total, 1, 3);

  auto result = detect_constant_bands(shuffled.data(), total, 1, 3);

  // Band 1 (component 1) should be constant
  REQUIRE((result.band_mask & (1u << 1)) != 0);
  // Bands 0 and 2 should not be constant
  REQUIRE((result.band_mask & (1u << 0)) == 0);
  REQUIRE((result.band_mask & (1u << 2)) == 0);
  REQUIRE(result.constant_values.size() == 1);
  REQUIRE(result.constant_values[0] == 0x42);
  // Compacted should have 2 bands * 3 elements = 6 bytes
  REQUIRE(result.compacted_data.size() == 6);
}

TEST_CASE("detect then restore round trip")
{
  // 5 elements of u32 x 2 components => stride=8, 8 bands of 5 bytes each
  // Component 0 varies, component 1 constant (0xDEADBEEF)
  uint32_t src[10];
  for (int i = 0; i < 5; i++)
  {
    src[i * 2] = static_cast<uint32_t>(i * 1000 + 42);
    src[i * 2 + 1] = 0xDEADBEEF;
  }
  uint32_t total = sizeof(src);

  std::vector<uint8_t> shuffled(total);
  byte_shuffle(reinterpret_cast<uint8_t *>(src), shuffled.data(), total, 4, 2);

  auto result = detect_constant_bands(shuffled.data(), total, 4, 2);

  // Restore
  std::vector<uint8_t> restored(total);
  restore_constant_bands(result.compacted_data.data(), uint32_t(result.compacted_data.size()),
                         restored.data(), total, result.band_mask,
                         result.constant_values.data(), 4, 2);

  REQUIRE(memcmp(shuffled.data(), restored.data(), total) == 0);
}

TEST_CASE("detect_constant_bands >32 bands graceful degradation")
{
  // typesize=8, components=5 would be 40 bands — exceeds 32-bit mask
  // Use components_4x4 = 5 but manually set up data
  uint32_t element_count = 4;
  uint32_t typesize = 8;
  uint32_t components = 5;
  uint32_t stride = typesize * components;
  uint32_t total = stride * element_count;

  std::vector<uint8_t> shuffled(total, 0x42);

  auto result = detect_constant_bands(shuffled.data(), total, uint8_t(typesize), uint8_t(components));

  REQUIRE(result.band_mask == 0);
  REQUIRE(result.compacted_data.size() == total);
  REQUIRE(memcmp(result.compacted_data.data(), shuffled.data(), total) == 0);
}
