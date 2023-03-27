#include <catch2/catch.hpp>
#include <fmt/printf.h>
#include "fixed_size_vector.hpp"

#include <catch2/catch.hpp>

TEST_CASE("fixed_capacity_vector_t construction and basic properties", "[fixed_capacity_vector_t]") {
  points::fixed_capacity_vector_t<int> vec(5);

  REQUIRE(vec.capacity() == 5);
}

TEST_CASE("fixed_capacity_vector_t element initialization and access", "[fixed_capacity_vector_t]") {
  points::fixed_capacity_vector_t<int> vec(3);

  vec.initialize_at(0, 1);
  vec.initialize_at(1, 2);
  vec.initialize_at(2, 3);

  REQUIRE(vec[0] == 1);
  REQUIRE(vec[1] == 2);
  REQUIRE(vec[2] == 3);
}

TEST_CASE("fixed_capacity_vector_t iterator usage", "[fixed_capacity_vector_t]") {
  points::fixed_capacity_vector_t<int> vec(3);

  vec.initialize_at(0, 1);
  vec.initialize_at(1, 2);
  vec.initialize_at(2, 3);

  auto it = vec.begin();
  REQUIRE(*it == 1);
  ++it;
  REQUIRE(*it == 2);
  ++it;
  REQUIRE(*it == 3);
  ++it;
  REQUIRE(it == vec.end());
}

TEST_CASE("fixed_capacity_vector_t const iterator usage", "[fixed_capacity_vector_t]") {
  points::fixed_capacity_vector_t<int> vec(3);

  vec.initialize_at(0,1);
  vec.initialize_at(1, 2);
  vec.initialize_at(2, 3);
  auto cit = vec.cbegin();
  REQUIRE(*cit == 1);
  ++cit;
  REQUIRE(*cit == 2);
  ++cit;
  REQUIRE(*cit == 3);
  ++cit;
  REQUIRE(cit == vec.cend());
}

TEST_CASE("fixed_capacity_vector_t move constructor", "[fixed_capacity_vector_t]") {
  points::fixed_capacity_vector_t<int> vec(3);
  vec.initialize_at(0, 1);
  vec.initialize_at(1, 2);
  vec.initialize_at(2, 3);

  points::fixed_capacity_vector_t<int> moved_vec(std::move(vec));

  REQUIRE(moved_vec.capacity() == 3);
  REQUIRE(moved_vec[0] == 1);
  REQUIRE(moved_vec[1] == 2);
  REQUIRE(moved_vec[2] == 3);

}

TEST_CASE("fixed_capacity_vector_t move assignment", "[fixed_capacity_vector_t]") {
  points::fixed_capacity_vector_t<int> vec(3);
  vec.initialize_at(0, 1);
  vec.initialize_at(1, 2);
  vec.initialize_at(2, 3);

  points::fixed_capacity_vector_t<int> moved_vec(1);
  moved_vec = std::move(vec);

  REQUIRE(moved_vec.capacity() == 3);
  REQUIRE(moved_vec[0] == 1);
  REQUIRE(moved_vec[1] == 2);
  REQUIRE(moved_vec[2] == 3);

}

TEST_CASE("fixed_capacity_vector_t clear", "[fixed_capacity_vector_t]")
{
  points::fixed_capacity_vector_t<int> vec(3);
  vec.initialize_at(0, 1);
  vec.initialize_at(1, 2);
  vec.initialize_at(2, 3);

  vec.clear();
  REQUIRE(vec.capacity() == 0);
}

TEST_CASE("fixed_capacity_vector_t noexcept move assignment", "[fixed_capacity_vector_t]") {
  points::fixed_capacity_vector_t<int> vec(3);
  vec.initialize_at(0, 1);
  vec.initialize_at(1, 2);
  vec.initialize_at(2, 3);

  points::fixed_capacity_vector_t<int> moved_vec(1);
  REQUIRE(std::is_nothrow_move_assignable_v<decltype(moved_vec)>);

  moved_vec = std::move(vec);

  REQUIRE(moved_vec.capacity() == 3);
  REQUIRE(moved_vec[0] == 1);
  REQUIRE(moved_vec[1] == 2);
  REQUIRE(moved_vec[2] == 3);
}

TEST_CASE("fixed_capacity_vector_t deleted copy constructor and assignment", "[fixed_capacity_vector_t]") {
  REQUIRE_FALSE(std::is_copy_constructible_v<points::fixed_capacity_vector_t<int>>);
  REQUIRE_FALSE(std::is_copy_assignable_v<points::fixed_capacity_vector_t<int>>);
}
