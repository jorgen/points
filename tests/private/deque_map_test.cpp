#include <catch2/catch.hpp>
#include <deque_map.hpp>



// Helper function to create a map and pre-populate it with some data
template<typename key_factory_t>
deque_map_t<int, std::string> create_pre_populated_map(key_factory_t key_factory) {
    deque_map_t<int, std::string> map;

    auto one = map.emplace_back(key_factory(), "value1");
    auto two = map.emplace_back(key_factory(), "value2");
    return map;
}

TEST_CASE("deque_map_t functionality", "[deque_map]")
{
    int last_key = 0;
    auto test_key_factory = [&last_key]() { return ++last_key; };
    auto map = create_pre_populated_map(test_key_factory);

    SECTION("Constructor initializes correctly")
    {
      REQUIRE(map.size() == 2);
    }

    SECTION("emplace_back adds values and returns reference")
    {
      auto value_ref = map.emplace_back(test_key_factory(), "test");
      REQUIRE(value_ref == "test");
    }

    SECTION("get_value retrieves the correct value for a given key")
    {
      auto opt_value = map.value(1);
      REQUIRE(opt_value == "value1");
    }

    SECTION("contains_key returns the correct boolean")
    {
      REQUIRE(map.contains_key(1));
      REQUIRE(map.contains_key(2));
      REQUIRE_FALSE(map.contains_key(3)); // Key 3 hasn't been added yet
    }

    SECTION("remove successfully removes key-value pair")
    {
      REQUIRE(map.remove(1));
      REQUIRE_FALSE(map.contains_key(1));
      REQUIRE_FALSE(map.remove(3)); // Key 3 was never added
    }

    SECTION("Consistent state after multiple emplace_back and remove operations")
    {
      auto value_ref = map.emplace_back(test_key_factory(), "test3");
      REQUIRE(value_ref == "test3");
      REQUIRE(map.remove(last_key));
      REQUIRE_FALSE(map.contains_key(last_key));
    }
}
