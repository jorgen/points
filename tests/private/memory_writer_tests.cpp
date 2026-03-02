#include <catch2/catch.hpp>
#include "memory_writer.hpp"

#include <vector>
#include <cstdint>

TEST_CASE("write_vec_type with empty vector succeeds", "[memory_writer]")
{
  uint8_t buffer[64];
  uint8_t *ptr = buffer;
  const uint8_t *end = buffer + sizeof(buffer);

  std::vector<uint32_t> empty_data;
  REQUIRE(write_vec_type(ptr, end, empty_data));
  REQUIRE(ptr == buffer);
}

TEST_CASE("read_vec_type with size zero succeeds", "[memory_writer]")
{
  uint8_t buffer[64];
  const uint8_t *ptr = buffer;
  const uint8_t *end = buffer + sizeof(buffer);

  std::vector<uint32_t> data;
  REQUIRE(read_vec_type(ptr, end, data, 0));
  REQUIRE(data.empty());
  REQUIRE(ptr == buffer);
}

TEST_CASE("write_vec_type and read_vec_type round-trip", "[memory_writer]")
{
  uint8_t buffer[256];
  uint8_t *write_ptr = buffer;
  const uint8_t *end = buffer + sizeof(buffer);

  std::vector<uint32_t> original = {1, 2, 3, 4, 5};
  REQUIRE(write_vec_type(write_ptr, end, original));
  REQUIRE(write_ptr == buffer + original.size() * sizeof(uint32_t));

  const uint8_t *read_ptr = buffer;
  std::vector<uint32_t> result;
  REQUIRE(read_vec_type(read_ptr, end, result, uint32_t(original.size())));
  REQUIRE(result == original);
}

TEST_CASE("write_vec_type fails when buffer too small", "[memory_writer]")
{
  uint8_t buffer[4];
  uint8_t *ptr = buffer;
  const uint8_t *end = buffer + sizeof(buffer);

  std::vector<uint32_t> data = {1, 2};
  REQUIRE_FALSE(write_vec_type(ptr, end, data));
}

TEST_CASE("read_vec_type fails when buffer too small", "[memory_writer]")
{
  uint8_t buffer[4];
  const uint8_t *ptr = buffer;
  const uint8_t *end = buffer + sizeof(buffer);

  std::vector<uint32_t> data;
  REQUIRE_FALSE(read_vec_type(ptr, end, data, 2));
}
