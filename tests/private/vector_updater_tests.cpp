#include <catch2/catch.hpp>
#include <fmt/printf.h>
#include "vector_updater.hpp"

struct source_t
{
  source_t(int data) : data(data) {}
  int data;
};

struct target_t
{
  target_t() {}
  target_t(int data) : data(data) {}
  target_t(source_t &&a) : data(a.data) {}
  int data;
};

TEST_CASE("vector_updater_start", "[vector]")
{
  std::vector<target_t> old = { 6, 7, 8 };
  std::vector<source_t> source = { 2,4,6,7,8 };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
  for (int i = 0; i < source.size(); i++)
  {
    REQUIRE(source[i].data == target[i].data);
  }
}
TEST_CASE("vector_updater_start_old", "[vector]")
{
  std::vector<target_t> old = { 2,4,6,7,8 };
  std::vector<source_t> source = { 6, 7, 8 };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
  for (int i = 0; i < source.size(); i++)
  {
    REQUIRE(source[i].data == target[i].data);
  }
}

TEST_CASE("vector_updater_end", "[vector]")
{
  std::vector<target_t> old = { 6, 7, 8 };
  std::vector<source_t> source = { 6,7,8, 9, 10, 11 };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
  for (int i = 0; i < source.size(); i++)
  {
    REQUIRE(source[i].data == target[i].data);
  }
}

TEST_CASE("vector_updater_end_old", "[vector]")
{
  std::vector<target_t> old = { 6,7,8, 9, 10, 11 };
  std::vector<source_t> source = { 6, 7, 8 };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
  for (int i = 0; i < source.size(); i++)
  {
    REQUIRE(source[i].data == target[i].data);
  }
}

TEST_CASE("vector_updater_middle", "[vector]")
{
  std::vector<target_t> old = { 6, 7, 8, 11 };
  std::vector<source_t> source = { 6,7,8, 9, 10, 11 };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
  for (int i = 0; i < source.size(); i++)
  {
    REQUIRE(source[i].data == target[i].data);
  }
}

TEST_CASE("vector_updater_middle_old", "[vector]")
{
  std::vector<target_t> old = { 6,7,8, 9, 10, 11 };
  std::vector<source_t> source = { 6, 7, 8, 11 };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
  for (int i = 0; i < source.size(); i++)
  {
    REQUIRE(source[i].data == target[i].data);
  }
}

TEST_CASE("vector_updater_empty_new", "[vector]")
{
  std::vector<target_t> old = { 6,7,8, 9, 10, 11 };
  std::vector<source_t> source = { };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
}

TEST_CASE("vector_updater_empty_old", "[vector]")
{
  std::vector<target_t> old = { };
  std::vector<source_t> source = { 6,7,8, 9, 10, 11 };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
}

TEST_CASE("vector_updater_empty_both", "[vector]")
{
  std::vector<target_t> old = { };
  std::vector<source_t> source = { 6,7,8, 9, 10, 11 };

  std::vector<target_t> target;
  update_vector(source, old, target, [](const target_t &a, const source_t &b) { return a.data - b.data; });
  REQUIRE(source.size() == target.size());
}

struct source_collection_t
{
  int level;
  std::vector<source_t> sources;
};

struct target_collection_t
{
  target_collection_t()
  {}
  target_collection_t(source_collection_t &&source)
  {
    targets.reserve(source.sources.size());
    for (auto &s : source.sources)
    {
      targets.emplace_back(s.data);
    }
    level = source.level;
  }

  target_collection_t(source_collection_t &source)
  {
    targets.reserve(source.sources.size());
    for (auto &s : source.sources)
    {
      targets.emplace_back(s.data);
    }
    level = source.level;
  }
  int level;
  std::vector<target_t> targets;
};


template<typename collection_t, typename member_t>
std::vector<collection_t> make_collection(std::vector<int> &levels, std::vector<int> &data, member_t member)
{
  std::vector<collection_t> ret;
  for (auto level : levels)
  {
    ret.emplace_back();
    auto &source_collection = ret.back();
    for (auto d : data)
    {
      (source_collection.*member).emplace_back(d);
    }
    source_collection.level = level;
  }
  return ret;
}

TEST_CASE("vector_multi_updater_start", "[vector]")
{
  std::vector<int> old_collection_d = {6,7,8,9};
  std::vector<int> old_d = { 6, 7, 8 };
  std::vector<int> source_collection_d = {2,3,4,5,6,7,8,9};
  std::vector<int> source_d = { 2,4,6,7,8 };

  auto old_collection = make_collection<target_collection_t>(old_collection_d, old_d, &target_collection_t::targets);
  auto source_collection = make_collection<source_collection_t>(source_collection_d, source_d, &source_collection_t::sources);

  std::vector<target_collection_t> target;
  using sit_t = decltype(source_collection)::iterator;
  using oit_t = decltype(old_collection)::iterator;
  update_vector(source_collection, old_collection, target,
                [](const target_collection_t &a, const source_collection_t &b) { return a.level - b.level; },
                [](oit_t oit, sit_t sit, std::vector<target_collection_t> &result)
  {
    result.emplace_back();
    result.back().level = sit->level;
    update_vector(sit->sources, oit->targets, result.back().targets, [](const target_t &a, const source_t &b) {return a.data - b.data; });
  }
   );
  REQUIRE(source_collection.size() == target.size());
  for (int level = 0; level < source_collection.size(); level++)
  {
    REQUIRE(source_collection[level].level == target[level].level);
    REQUIRE(source_collection[level].sources.size() == target[level].targets.size());
    REQUIRE(target[level].targets.size() == source_d.size());
    for (int i = 0; i < source_collection[level].sources.size(); i++)
    {
      REQUIRE(source_collection[level].sources[i].data == target[level].targets[i].data);
      REQUIRE(target[level].targets[i].data == source_d[i]);
    }
  }
}

TEST_CASE("update_vector with empty source vector")
{
  std::vector<int> source;
  std::vector<int> old{1, 2, 3};
  std::vector<int> result;
  update_vector(source, old, result,
    [](int a, int b) { return a - b; },
    [](auto, auto, auto&) {});

  REQUIRE(result.empty());
}

TEST_CASE("update_vector with empty old vector")
{
  std::vector<int> source{1, 2, 3};
  std::vector<int> old;
  std::vector<int> result;
  update_vector(source, old, result,
    [](int a, int b) { return a - b; },
    [](auto, auto, auto&) {});

  REQUIRE(result == source);
}

TEST_CASE("update_vector with no updates")
{
  std::vector<int> source{1, 2, 3};
  std::vector<int> old{1, 2, 3};
  std::vector<int> result;
  update_vector(source, old, result,
    [](int a, int b) { return a - b; },
    [](auto s, auto, auto&result) { result.emplace_back(std::move(*s));});

  REQUIRE(result == old);
}

TEST_CASE("update_vector with updates")
{
  std::vector<int> source{1, 2, 3};
  std::vector<int> old{0, 2, 4};
  std::vector<int> result;
  update_vector(source, old, result,
    [](int a, int b) { return a - b; },
    [](auto s, auto, auto&result) { result.emplace_back(std::move(*s));});

  REQUIRE(result == std::vector<int>({1, 2, 3}));
}

TEST_CASE("update_vector with updates and custom update function")
{
  std::vector<int> source{1, 2, 3};
  std::vector<int> old{0, 2, 4};
  std::vector<int> result;
  update_vector(source, old, result,
    [](int a, int b) { return a - b; },
    [](auto old_it, auto source_it, auto& result)
    {
      result.emplace_back(*old_it + *source_it);
    });

  REQUIRE(result == std::vector<int>({1, 4, 3}));
}
