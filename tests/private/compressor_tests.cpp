#include <doctest/doctest.h>
#include <attributes_configs.hpp>
#include <compressor.hpp>
#include <compressor_blosc2.hpp>
#include <compressor_zstd.hpp>
#include <compressor_fse.hpp>
#include <compressor_ans.hpp>
#include <compression_preprocess.hpp>
#include <input_header.hpp>
#include <points/converter/default_attribute_names.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

using namespace points::converter;

static std::vector<uint8_t> make_random_buffer(uint32_t size, uint32_t seed = 42)
{
  std::mt19937 gen(seed);
  std::uniform_int_distribution<uint32_t> dist(0, 255);
  std::vector<uint8_t> buf(size);
  for (auto &b : buf)
    b = static_cast<uint8_t>(dist(gen));
  return buf;
}

static std::vector<uint8_t> make_constant_buffer(uint32_t size, uint8_t value)
{
  return std::vector<uint8_t>(size, value);
}

static std::vector<uint8_t> make_sorted_u32_buffer(uint32_t count)
{
  std::vector<uint8_t> buf(count * 4);
  auto *values = reinterpret_cast<uint32_t *>(buf.data());
  for (uint32_t i = 0; i < count; i++)
    values[i] = i * 10;
  return buf;
}

// --- blosc2 round trip ---

TEST_CASE("blosc2 round trip")
{
  compressor_blosc2_t compressor;

  SUBCASE("random u8x1")
  {
    auto data = make_random_buffer(1024);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("random u32x3")
  {
    auto data = make_random_buffer(4 * 3 * 100);
    point_format_t fmt{points_type_u32, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("random r64x3")
  {
    auto data = make_random_buffer(8 * 3 * 50);
    point_format_t fmt{points_type_r64, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("constant buffer")
  {
    auto data = make_constant_buffer(1024, 0x42);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("sorted u32 morton")
  {
    auto data = make_sorted_u32_buffer(200);
    point_format_t fmt{points_type_m32, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("small buffer")
  {
    auto data = make_random_buffer(32);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }
}

// --- zstd round trip ---

TEST_CASE("zstd round trip")
{
  compressor_zstd_t compressor;

  SUBCASE("random u8x1")
  {
    auto data = make_random_buffer(1024);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("random u32x3")
  {
    auto data = make_random_buffer(4 * 3 * 100);
    point_format_t fmt{points_type_u32, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("sorted m192")
  {
    // 5 sorted 24-byte morton codes
    uint8_t data[24 * 5];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < 5; i++)
    {
      uint64_t val = static_cast<uint64_t>(i * 100);
      memcpy(data + i * 24, &val, 8);
    }
    point_format_t fmt{points_type_m192, points_components_1};
    auto compressed = compressor.compress(data, sizeof(data), fmt, 5);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == sizeof(data));
    REQUIRE(memcmp(decompressed.data.get(), data, sizeof(data)) == 0);
  }

  SUBCASE("constant u16x1")
  {
    uint32_t count = 200;
    std::vector<uint8_t> data(count * 2);
    for (uint32_t i = 0; i < count; i++)
    {
      uint16_t val = 0x1234;
      memcpy(data.data() + i * 2, &val, 2);
    }
    point_format_t fmt{points_type_u16, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("data with constant component")
  {
    // u32 x 2 components: component 0 varies, component 1 constant
    uint32_t count = 100;
    std::vector<uint8_t> data(count * 8);
    for (uint32_t i = 0; i < count; i++)
    {
      uint32_t varying = i * 7 + 3;
      uint32_t constant = 0xDEADBEEF;
      memcpy(data.data() + i * 8, &varying, 4);
      memcpy(data.data() + i * 8 + 4, &constant, 4);
    }
    point_format_t fmt{points_type_u32, points_components_2};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }
}

// --- huff0 round trip ---

TEST_CASE("huff0 round trip")
{
  compressor_huff0_t compressor;

  SUBCASE("random u8x1")
  {
    auto data = make_random_buffer(1024);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("random u32x3")
  {
    auto data = make_random_buffer(4 * 3 * 100);
    point_format_t fmt{points_type_u32, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("sorted m192")
  {
    uint8_t data[24 * 5];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < 5; i++)
    {
      uint64_t val = static_cast<uint64_t>(i * 100);
      memcpy(data + i * 24, &val, 8);
    }
    point_format_t fmt{points_type_m192, points_components_1};
    auto compressed = compressor.compress(data, sizeof(data), fmt, 5);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == sizeof(data));
    REQUIRE(memcmp(decompressed.data.get(), data, sizeof(data)) == 0);
  }

  SUBCASE("constant buffer")
  {
    auto data = make_constant_buffer(1024, 0x42);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("large buffer >128KB multi-chunk")
  {
    auto data = make_random_buffer(200000, 99);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }
}

// --- has_compression_magic ---

TEST_CASE("has_compression_magic")
{
  SUBCASE("valid header")
  {
    compression_header_t header;
    header.magic[0] = 'P';
    header.magic[1] = 'C';
    header.magic[2] = 'M';
    header.magic[3] = 1;
    header.method = compression_method_t::blosc2;
    header.type_size = 4;
    header.component_count = 1;
    header.flags = 0;
    header.uncompressed_size = 100;
    header.compressed_size = 50;
    REQUIRE(has_compression_magic(&header, sizeof(header)) == true);
  }

  SUBCASE("wrong magic")
  {
    uint8_t data[16] = {'P', 'C', 'X', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    REQUIRE(has_compression_magic(data, sizeof(data)) == false);
  }

  SUBCASE("too small")
  {
    uint8_t data[8] = {'P', 'C', 'M', 1, 0, 0, 0, 0};
    REQUIRE(has_compression_magic(data, 8) == false);
  }
}

// --- try_compress_constant ---

TEST_CASE("try_compress_constant constant buffer")
{
  uint32_t count = 100;
  std::vector<uint32_t> data(count, 0xDEADBEEF);
  point_format_t fmt{points_type_u32, points_components_1};
  auto result = try_compress_constant(data.data(), uint32_t(count * 4), fmt);
  REQUIRE(result.data.get() != nullptr);
  REQUIRE(result.size > 0);

  auto decompressed = decompress_any(result.data.get(), result.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == count * 4);
  REQUIRE(memcmp(decompressed.data.get(), data.data(), count * 4) == 0);
}

TEST_CASE("try_compress_constant non-constant buffer")
{
  uint32_t data[] = {1, 2, 3, 4};
  point_format_t fmt{points_type_u32, points_components_1};
  auto result = try_compress_constant(data, sizeof(data), fmt);
  REQUIRE(result.data.get() == nullptr);
}

TEST_CASE("try_compress_constant u8x3")
{
  uint32_t count = 50;
  // 3-byte elements, all identical
  std::vector<uint8_t> data(count * 3);
  for (uint32_t i = 0; i < count; i++)
  {
    data[i * 3 + 0] = 0x11;
    data[i * 3 + 1] = 0x22;
    data[i * 3 + 2] = 0x33;
  }
  point_format_t fmt{points_type_u8, points_components_3};
  auto result = try_compress_constant(data.data(), uint32_t(data.size()), fmt);
  REQUIRE(result.data.get() != nullptr);
  REQUIRE(result.size > 0);

  auto decompressed = decompress_any(result.data.get(), result.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

// --- decompress_any dispatches ---

TEST_CASE("decompress_any dispatches blosc2")
{
  auto data = make_random_buffer(1024);
  point_format_t fmt{points_type_u8, points_components_1};
  compressor_blosc2_t compressor;
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("decompress_any dispatches zstd")
{
  auto data = make_random_buffer(1024);
  point_format_t fmt{points_type_u8, points_components_1};
  compressor_zstd_t compressor;
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("decompress_any dispatches huff0")
{
  auto data = make_random_buffer(1024);
  point_format_t fmt{points_type_u8, points_components_1};
  compressor_huff0_t compressor;
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("decompress_any dispatches constant")
{
  uint32_t count = 100;
  std::vector<uint32_t> data(count, 0xCAFEBABE);
  point_format_t fmt{points_type_u32, points_components_1};
  auto compressed = try_compress_constant(data.data(), uint32_t(count * 4), fmt);
  REQUIRE(compressed.data.get() != nullptr);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == count * 4);
  REQUIRE(memcmp(decompressed.data.get(), data.data(), count * 4) == 0);
}

TEST_CASE("decompress_any rejects invalid magic")
{
  uint8_t data[32] = {};
  data[0] = 'X';
  auto result = decompress_any(data, sizeof(data));
  REQUIRE(result.error.code != 0);
}

TEST_CASE("decompress_any rejects too-small buffer")
{
  uint8_t data[8] = {'P', 'C', 'M', 1, 0, 0, 0, 0};
  auto result = decompress_any(data, 8);
  REQUIRE(result.error.code != 0);
}

// --- create_compressor ---

TEST_CASE("create_compressor returns correct types")
{
  auto blosc2 = create_compressor(compression_method_t::blosc2);
  REQUIRE(blosc2 != nullptr);
  REQUIRE(blosc2->method() == compression_method_t::blosc2);

  auto zstd = create_compressor(compression_method_t::zstd);
  REQUIRE(zstd != nullptr);
  REQUIRE(zstd->method() == compression_method_t::zstd);

  auto huff0 = create_compressor(compression_method_t::huff0);
  REQUIRE(huff0 != nullptr);
  REQUIRE(huff0->method() == compression_method_t::huff0);

  auto none = create_compressor(compression_method_t::none);
  REQUIRE(none == nullptr);

  auto constant = create_compressor(compression_method_t::constant);
  REQUIRE(constant == nullptr);
}

// --- compression_stats_t ---

TEST_CASE("compression_stats accumulate")
{
  compression_stats_t stats;
  point_format_t fmt{points_type_u32, points_components_3};

  stats.accumulate("position", fmt, 1000, 500);
  REQUIRE(stats.total_buffer_count == 1);
  REQUIRE(stats.per_attribute.size() == 1);
  REQUIRE(stats.per_attribute[0].name == "position");
  REQUIRE(stats.per_attribute[0].buffer_count == 1);
  REQUIRE(stats.per_attribute[0].uncompressed_bytes == 1000);
  REQUIRE(stats.per_attribute[0].compressed_bytes == 500);

  stats.accumulate("position", fmt, 2000, 800);
  REQUIRE(stats.total_buffer_count == 2);
  REQUIRE(stats.per_attribute.size() == 1);
  REQUIRE(stats.per_attribute[0].buffer_count == 2);
  REQUIRE(stats.per_attribute[0].uncompressed_bytes == 3000);
  REQUIRE(stats.per_attribute[0].compressed_bytes == 1300);

  point_format_t color_fmt{points_type_u8, points_components_4};
  stats.accumulate("color", color_fmt, 500, 200);
  REQUIRE(stats.total_buffer_count == 3);
  REQUIRE(stats.per_attribute.size() == 2);
  REQUIRE(stats.per_attribute[1].name == "color");
  REQUIRE(stats.per_attribute[1].buffer_count == 1);
}

TEST_CASE("compression_stats serialize/deserialize round trip")
{
  compression_stats_t stats;
  stats.input_file_count = 5;
  stats.method = compression_method_t::zstd;
  point_format_t fmt1{points_type_u32, points_components_3};
  point_format_t fmt2{points_type_u8, points_components_4};
  stats.accumulate("position", fmt1, 10000, 5000);
  stats.accumulate("position", fmt1, 20000, 8000);
  stats.accumulate("color", fmt2, 5000, 2000);

  uint32_t serialized_size = 0;
  auto serialized = stats.serialize(serialized_size);
  REQUIRE(serialized_size > 0);

  auto deserialized = compression_stats_t::deserialize(serialized.get(), serialized_size);
  REQUIRE(deserialized.input_file_count == stats.input_file_count);
  REQUIRE(deserialized.total_buffer_count == stats.total_buffer_count);
  REQUIRE(deserialized.method == stats.method);
  REQUIRE(deserialized.per_attribute.size() == stats.per_attribute.size());

  for (size_t i = 0; i < stats.per_attribute.size(); i++)
  {
    REQUIRE(deserialized.per_attribute[i].name == stats.per_attribute[i].name);
    REQUIRE(deserialized.per_attribute[i].format.type == stats.per_attribute[i].format.type);
    REQUIRE(deserialized.per_attribute[i].format.components == stats.per_attribute[i].format.components);
    REQUIRE(deserialized.per_attribute[i].buffer_count == stats.per_attribute[i].buffer_count);
    REQUIRE(deserialized.per_attribute[i].uncompressed_bytes == stats.per_attribute[i].uncompressed_bytes);
    REQUIRE(deserialized.per_attribute[i].compressed_bytes == stats.per_attribute[i].compressed_bytes);
  }
}

TEST_CASE("compression_stats serialize/deserialize empty")
{
  compression_stats_t stats;
  uint32_t serialized_size = 0;
  auto serialized = stats.serialize(serialized_size);
  REQUIRE(serialized_size > 0);

  auto deserialized = compression_stats_t::deserialize(serialized.get(), serialized_size);
  REQUIRE(deserialized.per_attribute.empty());
  REQUIRE(deserialized.total_buffer_count == 0);
}

TEST_CASE("compression_stats deserialize truncated")
{
  uint8_t data[10] = {};
  auto result = compression_stats_t::deserialize(data, 10);
  REQUIRE(result.per_attribute.empty());
  REQUIRE(result.total_buffer_count == 0);
}

TEST_CASE("compression_stats deserialize wrong version")
{
  uint8_t data[20] = {};
  uint32_t version = 99;
  memcpy(data, &version, 4);
  auto result = compression_stats_t::deserialize(data, sizeof(data));
  REQUIRE(result.per_attribute.empty());
  REQUIRE(result.total_buffer_count == 0);
}

// --- offset_subtract_f64 / offset_restore_f64 ---

TEST_CASE("offset_subtract_f64 round trip")
{
  double values[] = {100.5, 100.7, 100.6, 100.9, 100.1};
  uint32_t size = sizeof(values);

  std::vector<double> original(std::begin(values), std::end(values));
  double min_val = offset_subtract_f64(reinterpret_cast<uint8_t *>(values), size);

  REQUIRE(min_val == doctest::Approx(100.1));
  // All values should be >= 0 after subtraction
  for (auto &v : values)
    REQUIRE(v >= 0.0);

  offset_restore_f64(reinterpret_cast<uint8_t *>(values), size, min_val);
  for (size_t i = 0; i < original.size(); i++)
    REQUIRE(values[i] == doctest::Approx(original[i]));
}

TEST_CASE("offset_subtract_f64 single element")
{
  double values[] = {42.0};
  double min_val = offset_subtract_f64(reinterpret_cast<uint8_t *>(values), sizeof(values));
  REQUIRE(min_val == doctest::Approx(42.0));
  REQUIRE(values[0] == doctest::Approx(0.0));
  offset_restore_f64(reinterpret_cast<uint8_t *>(values), sizeof(values), min_val);
  REQUIRE(values[0] == doctest::Approx(42.0));
}

// --- sort_with_permutation_f64 / unsort_with_permutation_f64 ---

TEST_CASE("sort_with_permutation_f64 round trip")
{
  double values[] = {5.0, 1.0, 3.0, 2.0, 4.0};
  uint32_t count = 5;
  uint32_t size = count * 8;

  std::vector<double> original(std::begin(values), std::end(values));
  std::vector<uint16_t> perm(count);

  bool sorted = sort_with_permutation_f64(reinterpret_cast<uint8_t *>(values), size, perm.data());
  REQUIRE(sorted == true);

  // Verify sorted order
  for (uint32_t i = 1; i < count; i++)
    REQUIRE(values[i] >= values[i - 1]);

  unsort_with_permutation_f64(reinterpret_cast<uint8_t *>(values), size, perm.data());
  for (size_t i = 0; i < original.size(); i++)
    REQUIRE(values[i] == doctest::Approx(original[i]));
}

TEST_CASE("sort_with_permutation_f64 too many elements")
{
  std::vector<uint8_t> data(65536 * 8, 0);
  std::vector<uint16_t> perm(65536);
  bool sorted = sort_with_permutation_f64(data.data(), uint32_t(data.size()), perm.data());
  REQUIRE(sorted == false);
}

// --- zstd r64 compression round trip ---

TEST_CASE("zstd r64 offset compression round trip")
{
  compressor_zstd_t compressor;

  // Simulate GPS time data: narrow range of doubles
  uint32_t count = 1000;
  std::vector<double> values(count);
  std::mt19937 gen(123);
  std::uniform_real_distribution<double> dist(1.0e9, 1.0e9 + 100.0);
  for (auto &v : values)
    v = dist(gen);

  auto *data = reinterpret_cast<uint8_t *>(values.data());
  uint32_t size = count * 8;
  point_format_t fmt{points_type_r64, points_components_1};

  auto compressed = compressor.compress(data, size, fmt, 0);
  REQUIRE(compressed.error.code == 0);
  REQUIRE(compressed.data.get() != nullptr);

  auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == size);

  auto *result = reinterpret_cast<double *>(decompressed.data.get());
  for (uint32_t i = 0; i < count; i++)
    REQUIRE(result[i] == doctest::Approx(values[i]));
}

TEST_CASE("zstd r64 small buffer round trip")
{
  compressor_zstd_t compressor;

  double values[] = {1.0e9 + 0.5, 1.0e9 + 1.5, 1.0e9 + 0.1};
  uint32_t size = sizeof(values);
  point_format_t fmt{points_type_r64, points_components_1};

  auto compressed = compressor.compress(reinterpret_cast<uint8_t *>(values), size, fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == size);

  auto *result = reinterpret_cast<double *>(decompressed.data.get());
  for (int i = 0; i < 3; i++)
    REQUIRE(result[i] == doctest::Approx(values[i]));
}

TEST_CASE("huff0 r64 offset compression round trip")
{
  compressor_huff0_t compressor;

  uint32_t count = 500;
  std::vector<double> values(count);
  std::mt19937 gen(456);
  std::uniform_real_distribution<double> dist(1.0e9, 1.0e9 + 50.0);
  for (auto &v : values)
    v = dist(gen);

  auto *data = reinterpret_cast<uint8_t *>(values.data());
  uint32_t size = count * 8;
  point_format_t fmt{points_type_r64, points_components_1};

  auto compressed = compressor.compress(data, size, fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == size);

  auto *result = reinterpret_cast<double *>(decompressed.data.get());
  for (uint32_t i = 0; i < count; i++)
    REQUIRE(result[i] == doctest::Approx(values[i]));
}

// --- compression_stats with min/max ---

TEST_CASE("compression_stats accumulate with min/max")
{
  compression_stats_t stats;
  point_format_t fmt{points_type_r64, points_components_1};

  stats.accumulate("gps_time", fmt, 8000, 2000, 1.0e9, 1.0e9 + 50.0);
  REQUIRE(stats.per_attribute.size() == 1);
  REQUIRE(stats.per_attribute[0].min_value == doctest::Approx(1.0e9));
  REQUIRE(stats.per_attribute[0].max_value == doctest::Approx(1.0e9 + 50.0));

  stats.accumulate("gps_time", fmt, 8000, 2000, 1.0e9 - 10.0, 1.0e9 + 100.0);
  REQUIRE(stats.per_attribute[0].min_value == doctest::Approx(1.0e9 - 10.0));
  REQUIRE(stats.per_attribute[0].max_value == doctest::Approx(1.0e9 + 100.0));
}

TEST_CASE("compression_stats match by name and format")
{
  compression_stats_t stats;
  point_format_t fmt_m64{points_type_m64, points_components_1};
  point_format_t fmt_m128{points_type_m128, points_components_1};

  stats.accumulate("xyz", fmt_m64, 1000, 500);
  stats.accumulate("xyz", fmt_m128, 2000, 800);
  REQUIRE(stats.per_attribute.size() == 2);
  REQUIRE(stats.per_attribute[0].format.type == points_type_m64);
  REQUIRE(stats.per_attribute[1].format.type == points_type_m128);
}

TEST_CASE("compression_stats v2 serialize/deserialize with min/max")
{
  compression_stats_t stats;
  stats.input_file_count = 10;
  stats.method = compression_method_t::zstd;
  point_format_t fmt{points_type_r64, points_components_1};
  stats.accumulate("gps_time", fmt, 8000, 2000, 1.0e9, 1.0e9 + 100.0);

  uint32_t serialized_size = 0;
  auto serialized = stats.serialize(serialized_size);
  REQUIRE(serialized_size > 0);

  auto deserialized = compression_stats_t::deserialize(serialized.get(), serialized_size);
  REQUIRE(deserialized.per_attribute.size() == 1);
  REQUIRE(deserialized.per_attribute[0].min_value == doctest::Approx(1.0e9));
  REQUIRE(deserialized.per_attribute[0].max_value == doctest::Approx(1.0e9 + 100.0));
}

// --- decorrelate_u16x3 / correlate_u16x3 ---

static std::vector<uint8_t> make_correlated_u16x3_buffer(uint32_t count, uint32_t seed)
{
  std::mt19937 gen(seed);
  std::uniform_int_distribution<int> base_dist(20, 200);
  std::uniform_int_distribution<int> diff_dist(-10, 10);
  std::vector<uint8_t> buf(count * 6);
  for (uint32_t i = 0; i < count; i++)
  {
    int base = base_dist(gen);
    uint16_t r = static_cast<uint16_t>(std::clamp(base + diff_dist(gen), 0, 255) << 8);
    uint16_t g = static_cast<uint16_t>(std::clamp(base + diff_dist(gen), 0, 255) << 8);
    uint16_t b = static_cast<uint16_t>(std::clamp(base + diff_dist(gen), 0, 255) << 8);
    memcpy(buf.data() + i * 6 + 0, &r, 2);
    memcpy(buf.data() + i * 6 + 2, &g, 2);
    memcpy(buf.data() + i * 6 + 4, &b, 2);
  }
  return buf;
}

TEST_CASE("decorrelate_u16x3 round trip")
{
  SUBCASE("correlated data")
  {
    auto data = make_correlated_u16x3_buffer(500, 42);
    auto original = data;
    decorrelate_u16x3(data.data(), uint32_t(data.size()));
    REQUIRE(data != original);
    correlate_u16x3(data.data(), uint32_t(data.size()));
    REQUIRE(data == original);
  }

  SUBCASE("random data")
  {
    auto data = make_random_buffer(600, 99);
    auto original = data;
    decorrelate_u16x3(data.data(), uint32_t(data.size()));
    correlate_u16x3(data.data(), uint32_t(data.size()));
    REQUIRE(data == original);
  }

  SUBCASE("all zeros")
  {
    std::vector<uint8_t> data(600, 0);
    auto original = data;
    decorrelate_u16x3(data.data(), uint32_t(data.size()));
    correlate_u16x3(data.data(), uint32_t(data.size()));
    REQUIRE(data == original);
  }

  SUBCASE("single element")
  {
    uint16_t r = 0xFF00, g = 0x8000, b = 0x4000;
    uint8_t data[6];
    memcpy(data + 0, &r, 2);
    memcpy(data + 2, &g, 2);
    memcpy(data + 4, &b, 2);
    uint8_t original[6];
    memcpy(original, data, 6);
    decorrelate_u16x3(data, 6);
    correlate_u16x3(data, 6);
    REQUIRE(memcmp(data, original, 6) == 0);
  }
}

TEST_CASE("decorrelate_u16x3 preserves zero lower bytes")
{
  auto data = make_correlated_u16x3_buffer(200, 77);
  decorrelate_u16x3(data.data(), uint32_t(data.size()));
  // Each u16 component should still have 0x00 in the low byte
  // because shifted 8-bit values differ by at most 255, and (a<<8)-(b<<8) = (a-b)<<8
  for (uint32_t i = 0; i < data.size(); i += 2)
  {
    uint16_t val;
    memcpy(&val, data.data() + i, 2);
    REQUIRE((val & 0xFF) == 0);
  }
}

TEST_CASE("zstd u16x3 decorrelated compression round trip")
{
  compressor_zstd_t compressor;

  SUBCASE("correlated data")
  {
    auto data = make_correlated_u16x3_buffer(1000, 42);
    point_format_t fmt{points_type_u16, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("random data")
  {
    auto data = make_random_buffer(6 * 500, 99);
    point_format_t fmt{points_type_u16, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("small buffer")
  {
    auto data = make_correlated_u16x3_buffer(3, 11);
    point_format_t fmt{points_type_u16, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }
}

TEST_CASE("huff0 u16x3 decorrelated compression round trip")
{
  compressor_huff0_t compressor;

  SUBCASE("correlated data")
  {
    auto data = make_correlated_u16x3_buffer(1000, 42);
    point_format_t fmt{points_type_u16, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("random data")
  {
    auto data = make_random_buffer(6 * 500, 99);
    point_format_t fmt{points_type_u16, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("small buffer")
  {
    auto data = make_correlated_u16x3_buffer(3, 11);
    point_format_t fmt{points_type_u16, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }
}

// --- delta_encode_u16x3 / delta_decode_u16x3 ---

TEST_CASE("delta_encode_u16x3 round trip")
{
  SUBCASE("correlated data")
  {
    auto data = make_correlated_u16x3_buffer(500, 42);
    auto original = data;
    delta_encode_u16x3(data.data(), uint32_t(data.size()));
    REQUIRE(data != original);
    delta_decode_u16x3(data.data(), uint32_t(data.size()));
    REQUIRE(data == original);
  }

  SUBCASE("random data")
  {
    auto data = make_random_buffer(600, 99);
    auto original = data;
    delta_encode_u16x3(data.data(), uint32_t(data.size()));
    delta_decode_u16x3(data.data(), uint32_t(data.size()));
    REQUIRE(data == original);
  }

  SUBCASE("all zeros")
  {
    std::vector<uint8_t> data(600, 0);
    auto original = data;
    delta_encode_u16x3(data.data(), uint32_t(data.size()));
    delta_decode_u16x3(data.data(), uint32_t(data.size()));
    REQUIRE(data == original);
  }

  SUBCASE("single element")
  {
    uint16_t r = 0xFF00, g = 0x8000, b = 0x4000;
    uint8_t data[6];
    memcpy(data + 0, &r, 2);
    memcpy(data + 2, &g, 2);
    memcpy(data + 4, &b, 2);
    uint8_t original[6];
    memcpy(original, data, 6);
    delta_encode_u16x3(data, 6);
    delta_decode_u16x3(data, 6);
    REQUIRE(memcmp(data, original, 6) == 0);
  }
}

TEST_CASE("delta_encode_u16x3 preserves zero lower bytes")
{
  auto data = make_correlated_u16x3_buffer(200, 77);
  delta_encode_u16x3(data.data(), uint32_t(data.size()));
  for (uint32_t i = 0; i < data.size(); i += 2)
  {
    uint16_t val;
    memcpy(&val, data.data() + i, 2);
    REQUIRE((val & 0xFF) == 0);
  }
}

TEST_CASE("decorrelate + delta round trip")
{
  auto data = make_correlated_u16x3_buffer(1000, 55);
  auto original = data;
  decorrelate_u16x3(data.data(), uint32_t(data.size()));
  delta_encode_u16x3(data.data(), uint32_t(data.size()));
  delta_decode_u16x3(data.data(), uint32_t(data.size()));
  correlate_u16x3(data.data(), uint32_t(data.size()));
  REQUIRE(data == original);
}

// --- LOD stats ---

TEST_CASE("compression_stats accumulate with is_lod")
{
  compression_stats_t stats;
  point_format_t fmt{points_type_u32, points_components_3};

  // Source buffer
  stats.accumulate("position", fmt, 1000, 500, std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), 0, false);
  REQUIRE(stats.total_buffer_count == 1);
  REQUIRE(stats.lod_buffer_count == 0);
  REQUIRE(stats.per_attribute[0].buffer_count == 1);
  REQUIRE(stats.per_attribute[0].lod_buffer_count == 0);
  REQUIRE(stats.per_attribute[0].lod_uncompressed_bytes == 0);
  REQUIRE(stats.per_attribute[0].lod_compressed_bytes == 0);

  // LOD buffer
  stats.accumulate("position", fmt, 2000, 800, std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), 0, true);
  REQUIRE(stats.total_buffer_count == 2);
  REQUIRE(stats.lod_buffer_count == 1);
  REQUIRE(stats.per_attribute[0].buffer_count == 2);
  REQUIRE(stats.per_attribute[0].uncompressed_bytes == 3000);
  REQUIRE(stats.per_attribute[0].compressed_bytes == 1300);
  REQUIRE(stats.per_attribute[0].lod_buffer_count == 1);
  REQUIRE(stats.per_attribute[0].lod_uncompressed_bytes == 2000);
  REQUIRE(stats.per_attribute[0].lod_compressed_bytes == 800);

  // Another LOD buffer for a new attribute
  point_format_t color_fmt{points_type_u8, points_components_4};
  stats.accumulate("color", color_fmt, 500, 200, std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), 0, true);
  REQUIRE(stats.total_buffer_count == 3);
  REQUIRE(stats.lod_buffer_count == 2);
  REQUIRE(stats.per_attribute[1].lod_buffer_count == 1);
  REQUIRE(stats.per_attribute[1].lod_uncompressed_bytes == 500);
  REQUIRE(stats.per_attribute[1].lod_compressed_bytes == 200);
}

TEST_CASE("compression_stats v4 serialize/deserialize with LOD fields")
{
  compression_stats_t stats;
  stats.input_file_count = 3;
  stats.method = compression_method_t::zstd;
  point_format_t fmt1{points_type_u32, points_components_3};
  point_format_t fmt2{points_type_u8, points_components_4};

  // Mix of source and LOD
  stats.accumulate("position", fmt1, 10000, 5000, std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), 0, false);
  stats.accumulate("position", fmt1, 20000, 8000, std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), 0, true);
  stats.accumulate("color", fmt2, 5000, 2000, std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), 0, true);

  uint32_t serialized_size = 0;
  auto serialized = stats.serialize(serialized_size);
  REQUIRE(serialized_size > 0);

  auto deserialized = compression_stats_t::deserialize(serialized.get(), serialized_size);
  REQUIRE(deserialized.input_file_count == 3);
  REQUIRE(deserialized.total_buffer_count == 3);
  REQUIRE(deserialized.lod_buffer_count == 2);
  REQUIRE(deserialized.method == compression_method_t::zstd);
  REQUIRE(deserialized.per_attribute.size() == 2);

  // position
  REQUIRE(deserialized.per_attribute[0].buffer_count == 2);
  REQUIRE(deserialized.per_attribute[0].uncompressed_bytes == 30000);
  REQUIRE(deserialized.per_attribute[0].compressed_bytes == 13000);
  REQUIRE(deserialized.per_attribute[0].lod_buffer_count == 1);
  REQUIRE(deserialized.per_attribute[0].lod_uncompressed_bytes == 20000);
  REQUIRE(deserialized.per_attribute[0].lod_compressed_bytes == 8000);

  // color
  REQUIRE(deserialized.per_attribute[1].buffer_count == 1);
  REQUIRE(deserialized.per_attribute[1].lod_buffer_count == 1);
  REQUIRE(deserialized.per_attribute[1].lod_uncompressed_bytes == 5000);
  REQUIRE(deserialized.per_attribute[1].lod_compressed_bytes == 2000);
}

// --- delta_encode_single / delta_decode_single ---

TEST_CASE("delta_encode_single round trip u8")
{
  std::vector<uint8_t> data = {10, 12, 15, 14, 20, 20, 25};
  auto original = data;
  delta_encode_single(data.data(), uint32_t(data.size()), 1);
  REQUIRE(data != original);
  delta_decode_single(data.data(), uint32_t(data.size()), 1);
  REQUIRE(data == original);
}

TEST_CASE("delta_encode_single round trip u16")
{
  uint32_t count = 200;
  std::vector<uint8_t> data(count * 2);
  for (uint32_t i = 0; i < count; i++)
  {
    uint16_t val = static_cast<uint16_t>(1000 + i * 3);
    memcpy(data.data() + i * 2, &val, 2);
  }
  auto original = data;
  delta_encode_single(data.data(), uint32_t(data.size()), 2);
  REQUIRE(data != original);
  delta_decode_single(data.data(), uint32_t(data.size()), 2);
  REQUIRE(data == original);
}

TEST_CASE("delta_encode_single round trip u32")
{
  uint32_t count = 100;
  std::vector<uint8_t> data(count * 4);
  for (uint32_t i = 0; i < count; i++)
  {
    uint32_t val = 50000 + i * 7;
    memcpy(data.data() + i * 4, &val, 4);
  }
  auto original = data;
  delta_encode_single(data.data(), uint32_t(data.size()), 4);
  REQUIRE(data != original);
  delta_decode_single(data.data(), uint32_t(data.size()), 4);
  REQUIRE(data == original);
}

TEST_CASE("delta_encode_single round trip random data")
{
  auto data = make_random_buffer(512, 77);
  auto original = data;
  delta_encode_single(data.data(), uint32_t(data.size()), 2);
  delta_decode_single(data.data(), uint32_t(data.size()), 2);
  REQUIRE(data == original);
}

TEST_CASE("delta_encode_single single element")
{
  uint16_t val = 0x1234;
  uint8_t data[2];
  memcpy(data, &val, 2);
  uint8_t original[2];
  memcpy(original, data, 2);
  delta_encode_single(data, 2, 2);
  REQUIRE(memcmp(data, original, 2) == 0); // no change for single element
}

TEST_CASE("zstd element delta round trip gradually changing u16x1")
{
  compressor_zstd_t compressor;
  uint32_t count = 1000;
  std::vector<uint8_t> data(count * 2);
  std::mt19937 gen(42);
  std::uniform_int_distribution<int> diff(-5, 5);
  uint16_t val = 30000;
  for (uint32_t i = 0; i < count; i++)
  {
    val = static_cast<uint16_t>(val + diff(gen));
    memcpy(data.data() + i * 2, &val, 2);
  }
  point_format_t fmt{points_type_u16, points_components_1};
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);
  auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("zstd element delta round trip random u16x1")
{
  compressor_zstd_t compressor;
  auto data = make_random_buffer(2000, 99);
  point_format_t fmt{points_type_u16, points_components_1};
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);
  auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("huff0 element delta round trip gradually changing u16x1")
{
  compressor_huff0_t compressor;
  uint32_t count = 1000;
  std::vector<uint8_t> data(count * 2);
  std::mt19937 gen(42);
  std::uniform_int_distribution<int> diff(-5, 5);
  uint16_t val = 30000;
  for (uint32_t i = 0; i < count; i++)
  {
    val = static_cast<uint16_t>(val + diff(gen));
    memcpy(data.data() + i * 2, &val, 2);
  }
  point_format_t fmt{points_type_u16, points_components_1};
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);
  auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("zstd element delta round trip u8x1")
{
  compressor_zstd_t compressor;
  uint32_t count = 500;
  std::vector<uint8_t> data(count);
  for (uint32_t i = 0; i < count; i++)
    data[i] = static_cast<uint8_t>(100 + (i % 5));
  point_format_t fmt{points_type_u8, points_components_1};
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);
  auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("zstd element delta round trip u32x1")
{
  compressor_zstd_t compressor;
  uint32_t count = 300;
  std::vector<uint8_t> data(count * 4);
  for (uint32_t i = 0; i < count; i++)
  {
    uint32_t val = 100000 + i * 10;
    memcpy(data.data() + i * 4, &val, 4);
  }
  point_format_t fmt{points_type_u32, points_components_1};
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);
  auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("compression_stats v3 backward compat deserialize has zero LOD")
{
  // Build a v3 blob manually: serialize with old code layout
  compression_stats_t stats;
  stats.input_file_count = 5;
  stats.method = compression_method_t::zstd;
  point_format_t fmt{points_type_u32, points_components_3};
  stats.accumulate("position", fmt, 10000, 5000);

  // Manually build v3 serialized blob
  uint32_t name_size = 8; // "position"
  uint32_t v3_header = 4 + 4 + 4 + 1 + 3 + 4; // 20 bytes
  uint32_t v3_per_attr = 4 + name_size + 1 + 1 + 2 + 8 + 8 + 8 + 8 + 8 + 32; // 88 bytes
  uint32_t total_v3 = v3_header + v3_per_attr;
  std::vector<uint8_t> v3_data(total_v3, 0);
  auto *ptr = v3_data.data();

  uint32_t version = 3;
  memcpy(ptr, &version, 4); ptr += 4;
  uint32_t ifc = 5;
  memcpy(ptr, &ifc, 4); ptr += 4;
  uint32_t tbc = 1;
  memcpy(ptr, &tbc, 4); ptr += 4;
  uint8_t m = 2; // zstd
  memcpy(ptr, &m, 1); ptr += 1;
  ptr += 3; // padding
  uint32_t ac = 1;
  memcpy(ptr, &ac, 4); ptr += 4;

  // attribute
  uint32_t ns = 8;
  memcpy(ptr, &ns, 4); ptr += 4;
  memcpy(ptr, "position", 8); ptr += 8;
  uint8_t type_val = static_cast<uint8_t>(points_type_u32);
  uint8_t comp_val = static_cast<uint8_t>(points_components_3);
  memcpy(ptr, &type_val, 1); ptr += 1;
  memcpy(ptr, &comp_val, 1); ptr += 1;
  ptr += 2; // padding
  uint64_t bc = 1;
  memcpy(ptr, &bc, 8); ptr += 8;
  uint64_t ub = 10000;
  memcpy(ptr, &ub, 8); ptr += 8;
  uint64_t cb = 5000;
  memcpy(ptr, &cb, 8); ptr += 8;
  double minv = std::numeric_limits<double>::max();
  double maxv = std::numeric_limits<double>::lowest();
  memcpy(ptr, &minv, 8); ptr += 8;
  memcpy(ptr, &maxv, 8); ptr += 8;
  uint64_t path_counts[4] = {};
  path_counts[0] = 1;
  memcpy(ptr, path_counts, 32); ptr += 32;

  auto result = compression_stats_t::deserialize(v3_data.data(), total_v3);
  REQUIRE(result.input_file_count == 5);
  REQUIRE(result.total_buffer_count == 1);
  REQUIRE(result.lod_buffer_count == 0);
  REQUIRE(result.per_attribute.size() == 1);
  REQUIRE(result.per_attribute[0].name == "position");
  REQUIRE(result.per_attribute[0].buffer_count == 1);
  REQUIRE(result.per_attribute[0].lod_buffer_count == 0);
  REQUIRE(result.per_attribute[0].lod_uncompressed_bytes == 0);
  REQUIRE(result.per_attribute[0].lod_compressed_bytes == 0);
}

// --- ANS (FSE) round trip ---

TEST_CASE("ans round trip")
{
  compressor_ans_t compressor;

  SUBCASE("random u8x1")
  {
    auto data = make_random_buffer(1024);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("random u32x3")
  {
    auto data = make_random_buffer(4 * 3 * 100);
    point_format_t fmt{points_type_u32, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("sorted m192")
  {
    uint8_t data[24 * 5];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < 5; i++)
    {
      uint64_t val = static_cast<uint64_t>(i * 100);
      memcpy(data + i * 24, &val, 8);
    }
    point_format_t fmt{points_type_m192, points_components_1};
    auto compressed = compressor.compress(data, sizeof(data), fmt, 5);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == sizeof(data));
    REQUIRE(memcmp(decompressed.data.get(), data, sizeof(data)) == 0);
  }

  SUBCASE("constant buffer")
  {
    auto data = make_constant_buffer(1024, 0x42);
    point_format_t fmt{points_type_u8, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("u16x3 correlated")
  {
    auto data = make_correlated_u16x3_buffer(1000, 42);
    point_format_t fmt{points_type_u16, points_components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("gradually changing u16x1")
  {
    uint32_t count = 1000;
    std::vector<uint8_t> data(count * 2);
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> diff(-5, 5);
    uint16_t val = 30000;
    for (uint32_t i = 0; i < count; i++)
    {
      val = static_cast<uint16_t>(val + diff(gen));
      memcpy(data.data() + i * 2, &val, 2);
    }
    point_format_t fmt{points_type_u16, points_components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SUBCASE("r64 offset")
  {
    uint32_t count = 500;
    std::vector<double> values(count);
    std::mt19937 gen(456);
    std::uniform_real_distribution<double> dist(1.0e9, 1.0e9 + 50.0);
    for (auto &v : values)
      v = dist(gen);

    auto *data = reinterpret_cast<uint8_t *>(values.data());
    uint32_t size = count * 8;
    point_format_t fmt{points_type_r64, points_components_1};

    auto compressed = compressor.compress(data, size, fmt, 0);
    REQUIRE(compressed.error.code == 0);

    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == size);

    auto *result = reinterpret_cast<double *>(decompressed.data.get());
    for (uint32_t i = 0; i < count; i++)
      REQUIRE(result[i] == doctest::Approx(values[i]));
  }
}

TEST_CASE("decompress_any dispatches ans")
{
  auto data = make_random_buffer(1024);
  point_format_t fmt{points_type_u8, points_components_1};
  compressor_ans_t compressor;
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("create_compressor returns ans")
{
  auto ans = create_compressor(compression_method_t::ans);
  REQUIRE(ans != nullptr);
  REQUIRE(ans->method() == compression_method_t::ans);
}

// --- LOD attribute format excludes original_order ---

static points_converter_attributes_t make_test_attributes(std::initializer_list<std::tuple<const char *, points_type_t, points_components_t>> specs)
{
  points_converter_attributes_t attrs;
  for (auto &[name, type, comp] : specs)
  {
    auto len = uint32_t(strlen(name));
    auto buf = std::make_unique<char[]>(len + 1);
    memcpy(buf.get(), name, len + 1);
    attrs.attributes.emplace_back(buf.get(), len, type, comp);
    attrs.attribute_names.push_back(std::move(buf));
  }
  return attrs;
}

TEST_CASE("LOD attribute format excludes original_order")
{
  attributes_configs_t configs;
  auto attrs = make_test_attributes({
    {POINTS_ATTRIBUTE_XYZ, points_type_m64, points_components_1},
    {POINTS_ATTRIBUTE_RGB, points_type_u8, points_components_3},
    {POINTS_ATTRIBUTE_ORIGINAL_ORDER, points_type_u32, points_components_1},
  });
  REQUIRE(attrs.attributes.size() == 3);
  REQUIRE(attrs.attribute_names.size() == 3);

  auto source_id = configs.get_attribute_config_index(std::move(attrs));
  auto mapping = configs.get_lod_attribute_mapping(1, &source_id, &source_id + 1);

  auto &lod_attrs = configs.get(mapping.destination_id);
  REQUIRE(lod_attrs.attributes.size() == 2);
  for (auto &attr : lod_attrs.attributes)
  {
    bool is_original_order = attr.name_size == strlen(POINTS_ATTRIBUTE_ORIGINAL_ORDER) &&
                             memcmp(attr.name, POINTS_ATTRIBUTE_ORIGINAL_ORDER, attr.name_size) == 0;
    REQUIRE_FALSE(is_original_order);
  }
  REQUIRE(memcmp(lod_attrs.attributes[0].name, POINTS_ATTRIBUTE_XYZ, strlen(POINTS_ATTRIBUTE_XYZ)) == 0);
  REQUIRE(memcmp(lod_attrs.attributes[1].name, POINTS_ATTRIBUTE_RGB, strlen(POINTS_ATTRIBUTE_RGB)) == 0);
}

TEST_CASE("LOD attribute format without original_order is unchanged")
{
  attributes_configs_t configs;
  auto attrs = make_test_attributes({
    {POINTS_ATTRIBUTE_XYZ, points_type_m64, points_components_1},
    {POINTS_ATTRIBUTE_RGB, points_type_u8, points_components_3},
    {POINTS_ATTRIBUTE_INTENSITY, points_type_u16, points_components_1},
  });
  auto source_id = configs.get_attribute_config_index(std::move(attrs));
  auto mapping = configs.get_lod_attribute_mapping(1, &source_id, &source_id + 1);

  auto &lod_attrs = configs.get(mapping.destination_id);
  REQUIRE(lod_attrs.attributes.size() == 3);
  REQUIRE(memcmp(lod_attrs.attributes[0].name, POINTS_ATTRIBUTE_XYZ, strlen(POINTS_ATTRIBUTE_XYZ)) == 0);
  REQUIRE(memcmp(lod_attrs.attributes[1].name, POINTS_ATTRIBUTE_RGB, strlen(POINTS_ATTRIBUTE_RGB)) == 0);
  REQUIRE(memcmp(lod_attrs.attributes[2].name, POINTS_ATTRIBUTE_INTENSITY, strlen(POINTS_ATTRIBUTE_INTENSITY)) == 0);
}

