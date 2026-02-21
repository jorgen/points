#include <catch2/catch.hpp>
#include <compressor.hpp>
#include <compressor_blosc2.hpp>
#include <compressor_zstd.hpp>
#include <compressor_fse.hpp>
#include <input_header.hpp>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

using namespace points;
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

TEST_CASE("blosc2 round trip", "[converter]")
{
  compressor_blosc2_t compressor;

  SECTION("random u8x1")
  {
    auto data = make_random_buffer(1024);
    point_format_t fmt{type_u8, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("random u32x3")
  {
    auto data = make_random_buffer(4 * 3 * 100);
    point_format_t fmt{type_u32, components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("random r64x3")
  {
    auto data = make_random_buffer(8 * 3 * 50);
    point_format_t fmt{type_r64, components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("constant buffer")
  {
    auto data = make_constant_buffer(1024, 0x42);
    point_format_t fmt{type_u8, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("sorted u32 morton")
  {
    auto data = make_sorted_u32_buffer(200);
    point_format_t fmt{type_m32, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("small buffer")
  {
    auto data = make_random_buffer(32);
    point_format_t fmt{type_u8, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }
}

// --- zstd round trip ---

TEST_CASE("zstd round trip", "[converter]")
{
  compressor_zstd_t compressor;

  SECTION("random u8x1")
  {
    auto data = make_random_buffer(1024);
    point_format_t fmt{type_u8, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("random u32x3")
  {
    auto data = make_random_buffer(4 * 3 * 100);
    point_format_t fmt{type_u32, components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("sorted m192")
  {
    // 5 sorted 24-byte morton codes
    uint8_t data[24 * 5];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < 5; i++)
    {
      uint64_t val = static_cast<uint64_t>(i * 100);
      memcpy(data + i * 24, &val, 8);
    }
    point_format_t fmt{type_m192, components_1};
    auto compressed = compressor.compress(data, sizeof(data), fmt, 5);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == sizeof(data));
    REQUIRE(memcmp(decompressed.data.get(), data, sizeof(data)) == 0);
  }

  SECTION("constant u16x1")
  {
    uint32_t count = 200;
    std::vector<uint8_t> data(count * 2);
    for (uint32_t i = 0; i < count; i++)
    {
      uint16_t val = 0x1234;
      memcpy(data.data() + i * 2, &val, 2);
    }
    point_format_t fmt{type_u16, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("data with constant component")
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
    point_format_t fmt{type_u32, components_2};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }
}

// --- huff0 round trip ---

TEST_CASE("huff0 round trip", "[converter]")
{
  compressor_huff0_t compressor;

  SECTION("random u8x1")
  {
    auto data = make_random_buffer(1024);
    point_format_t fmt{type_u8, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("random u32x3")
  {
    auto data = make_random_buffer(4 * 3 * 100);
    point_format_t fmt{type_u32, components_3};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("sorted m192")
  {
    uint8_t data[24 * 5];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < 5; i++)
    {
      uint64_t val = static_cast<uint64_t>(i * 100);
      memcpy(data + i * 24, &val, 8);
    }
    point_format_t fmt{type_m192, components_1};
    auto compressed = compressor.compress(data, sizeof(data), fmt, 5);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == sizeof(data));
    REQUIRE(memcmp(decompressed.data.get(), data, sizeof(data)) == 0);
  }

  SECTION("constant buffer")
  {
    auto data = make_constant_buffer(1024, 0x42);
    point_format_t fmt{type_u8, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }

  SECTION("large buffer >128KB multi-chunk")
  {
    auto data = make_random_buffer(200000, 99);
    point_format_t fmt{type_u8, components_1};
    auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
    REQUIRE(compressed.error.code == 0);
    auto decompressed = compressor.decompress(compressed.data.get(), compressed.size);
    REQUIRE(decompressed.error.code == 0);
    REQUIRE(decompressed.size == data.size());
    REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
  }
}

// --- has_compression_magic ---

TEST_CASE("has_compression_magic", "[converter]")
{
  SECTION("valid header")
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

  SECTION("wrong magic")
  {
    uint8_t data[16] = {'P', 'C', 'X', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    REQUIRE(has_compression_magic(data, sizeof(data)) == false);
  }

  SECTION("too small")
  {
    uint8_t data[8] = {'P', 'C', 'M', 1, 0, 0, 0, 0};
    REQUIRE(has_compression_magic(data, 8) == false);
  }
}

// --- try_compress_constant ---

TEST_CASE("try_compress_constant constant buffer", "[converter]")
{
  uint32_t count = 100;
  std::vector<uint32_t> data(count, 0xDEADBEEF);
  point_format_t fmt{type_u32, components_1};
  auto result = try_compress_constant(data.data(), uint32_t(count * 4), fmt);
  REQUIRE(result.data != nullptr);
  REQUIRE(result.size > 0);

  auto decompressed = decompress_any(result.data.get(), result.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == count * 4);
  REQUIRE(memcmp(decompressed.data.get(), data.data(), count * 4) == 0);
}

TEST_CASE("try_compress_constant non-constant buffer", "[converter]")
{
  uint32_t data[] = {1, 2, 3, 4};
  point_format_t fmt{type_u32, components_1};
  auto result = try_compress_constant(data, sizeof(data), fmt);
  REQUIRE(result.data == nullptr);
}

TEST_CASE("try_compress_constant u8x3", "[converter]")
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
  point_format_t fmt{type_u8, components_3};
  auto result = try_compress_constant(data.data(), uint32_t(data.size()), fmt);
  REQUIRE(result.data != nullptr);
  REQUIRE(result.size > 0);

  auto decompressed = decompress_any(result.data.get(), result.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

// --- decompress_any dispatches ---

TEST_CASE("decompress_any dispatches blosc2", "[converter]")
{
  auto data = make_random_buffer(1024);
  point_format_t fmt{type_u8, components_1};
  compressor_blosc2_t compressor;
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("decompress_any dispatches zstd", "[converter]")
{
  auto data = make_random_buffer(1024);
  point_format_t fmt{type_u8, components_1};
  compressor_zstd_t compressor;
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("decompress_any dispatches huff0", "[converter]")
{
  auto data = make_random_buffer(1024);
  point_format_t fmt{type_u8, components_1};
  compressor_huff0_t compressor;
  auto compressed = compressor.compress(data.data(), uint32_t(data.size()), fmt, 0);
  REQUIRE(compressed.error.code == 0);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == data.size());
  REQUIRE(memcmp(decompressed.data.get(), data.data(), data.size()) == 0);
}

TEST_CASE("decompress_any dispatches constant", "[converter]")
{
  uint32_t count = 100;
  std::vector<uint32_t> data(count, 0xCAFEBABE);
  point_format_t fmt{type_u32, components_1};
  auto compressed = try_compress_constant(data.data(), uint32_t(count * 4), fmt);
  REQUIRE(compressed.data != nullptr);

  auto decompressed = decompress_any(compressed.data.get(), compressed.size);
  REQUIRE(decompressed.error.code == 0);
  REQUIRE(decompressed.size == count * 4);
  REQUIRE(memcmp(decompressed.data.get(), data.data(), count * 4) == 0);
}

TEST_CASE("decompress_any rejects invalid magic", "[converter]")
{
  uint8_t data[32] = {};
  data[0] = 'X';
  auto result = decompress_any(data, sizeof(data));
  REQUIRE(result.error.code != 0);
}

TEST_CASE("decompress_any rejects too-small buffer", "[converter]")
{
  uint8_t data[8] = {'P', 'C', 'M', 1, 0, 0, 0, 0};
  auto result = decompress_any(data, 8);
  REQUIRE(result.error.code != 0);
}

// --- create_compressor ---

TEST_CASE("create_compressor returns correct types", "[converter]")
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

TEST_CASE("compression_stats accumulate", "[converter]")
{
  compression_stats_t stats;
  point_format_t fmt{type_u32, components_3};

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

  point_format_t color_fmt{type_u8, components_4};
  stats.accumulate("color", color_fmt, 500, 200);
  REQUIRE(stats.total_buffer_count == 3);
  REQUIRE(stats.per_attribute.size() == 2);
  REQUIRE(stats.per_attribute[1].name == "color");
  REQUIRE(stats.per_attribute[1].buffer_count == 1);
}

TEST_CASE("compression_stats serialize/deserialize round trip", "[converter]")
{
  compression_stats_t stats;
  stats.input_file_count = 5;
  stats.method = compression_method_t::zstd;
  point_format_t fmt1{type_u32, components_3};
  point_format_t fmt2{type_u8, components_4};
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

TEST_CASE("compression_stats serialize/deserialize empty", "[converter]")
{
  compression_stats_t stats;
  uint32_t serialized_size = 0;
  auto serialized = stats.serialize(serialized_size);
  REQUIRE(serialized_size > 0);

  auto deserialized = compression_stats_t::deserialize(serialized.get(), serialized_size);
  REQUIRE(deserialized.per_attribute.empty());
  REQUIRE(deserialized.total_buffer_count == 0);
}

TEST_CASE("compression_stats deserialize truncated", "[converter]")
{
  uint8_t data[10] = {};
  auto result = compression_stats_t::deserialize(data, 10);
  REQUIRE(result.per_attribute.empty());
  REQUIRE(result.total_buffer_count == 0);
}

TEST_CASE("compression_stats deserialize wrong version", "[converter]")
{
  uint8_t data[20] = {};
  uint32_t version = 99;
  memcpy(data, &version, 4);
  auto result = compression_stats_t::deserialize(data, sizeof(data));
  REQUIRE(result.per_attribute.empty());
  REQUIRE(result.total_buffer_count == 0);
}
