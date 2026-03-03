#include <doctest/doctest.h>
#include <frame_node_registry.hpp>
#include <gpu_node_buffer.hpp>
#include <node_selector.hpp>

using namespace points;
using namespace points::converter;

static node_id_t make_nid(uint32_t tree, uint16_t level, uint16_t index)
{
  node_id_t id;
  id.tree_id.data = tree;
  id.level = level;
  id.index = index;
  return id;
}

static tree_walker_data_t make_sub(node_id_t node, node_id_t parent, int lod, uint32_t point_count = 1000)
{
  tree_walker_data_t d;
  d.node = node;
  d.parent = parent;
  d.lod = lod;
  d.point_count = point_count_t(point_count);
  d.frustum_visible = true;
  d.aabb = {{0, 0, 0}, {1, 1, 1}};
  d.tight_aabb = {{0.1, 0.1, 0.1}, {0.9, 0.9, 0.9}};
  return d;
}

static void setup_registry(frame_node_registry_t &registry,
                           const std::vector<tree_walker_data_t> &subsets,
                           const std::vector<std::pair<node_id_t, node_id_t>> &edges,
                           const std::vector<bool> &rendered = {},
                           const std::vector<size_t> &gpu_mem = {})
{
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  for (size_t i = 0; i < subsets.size(); i++)
  {
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = subsets[i];
    if (i < rendered.size() && rendered[i])
    {
      b->rendered = true;
      b->fade_frame = gpu_node_buffer_t::FADE_FRAMES; // fade complete
    }
    if (i < gpu_mem.size())
      b->gpu_memory_size = gpu_mem[i];
    bufs.push_back(std::move(b));
  }
  registry.update_from_walker(subsets, edges, bufs);
}

TEST_CASE("node_selector selection")
{
  node_selector_t selector;
  frame_node_registry_t registry;

  SUBCASE("empty registry produces empty active set")
  {
    selection_params_t params;
    params.camera_position = {0, 0, 0};
    auto result = selector.select(registry, params);
    REQUIRE(result.active_set.empty());
    REQUIRE(result.total_points == 0);
  }

  SUBCASE("single root loaded -> root in active set")
  {
    auto root = make_nid(1, 0, 0);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10, 5000)};
    setup_registry(registry, subsets, {}, {true}, {1024});

    selection_params_t params;
    params.camera_position = {0, 0, 0};
    params.memory_budget = 1024 * 1024;
    params.point_budget = 100000;

    auto result = selector.select(registry, params);
    REQUIRE(result.active_set.size() == 1);
    REQUIRE(result.active_set.count(root));
  }

  SUBCASE("root + all children loaded and faded -> children replace root")
  {
    auto root = make_nid(1, 0, 0);
    auto child_a = make_nid(1, 1, 0);
    auto child_b = make_nid(1, 1, 1);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {
      make_sub(root, empty, 10, 5000),
      make_sub(child_a, root, 9, 3000),
      make_sub(child_b, root, 9, 3000),
    };
    std::vector<std::pair<node_id_t, node_id_t>> edges = {
      {root, child_a},
      {root, child_b},
    };
    setup_registry(registry, subsets, edges, {true, true, true}, {1024, 512, 512});

    selection_params_t params;
    params.camera_position = {0.5, 0.5, 0.5};
    params.memory_budget = 1024 * 1024;
    params.point_budget = 100000;

    auto result = selector.select(registry, params);
    // Children should replace root (clean swap)
    REQUIRE(result.active_set.count(child_a));
    REQUIRE(result.active_set.count(child_b));
    REQUIRE(!result.active_set.count(root));
  }

  SUBCASE("root + partial children -> mixed covering")
  {
    auto root = make_nid(1, 0, 0);
    auto child_a = make_nid(1, 1, 0);
    auto child_b = make_nid(1, 1, 1);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {
      make_sub(root, empty, 10, 5000),
      make_sub(child_a, root, 9, 3000),
      make_sub(child_b, root, 9, 3000),
    };
    std::vector<std::pair<node_id_t, node_id_t>> edges = {
      {root, child_a},
      {root, child_b},
    };
    // Only child_a is rendered
    setup_registry(registry, subsets, edges, {true, true, false}, {1024, 512, 0});

    selection_params_t params;
    params.camera_position = {0.5, 0.5, 0.5};
    params.memory_budget = 1024 * 1024;
    params.point_budget = 100000;

    auto result = selector.select(registry, params);
    // Root stays as fallback, child_a also active
    REQUIRE(result.active_set.count(root));
    REQUIRE(result.active_set.count(child_a));
    REQUIRE(!result.active_set.count(child_b));
  }

  SUBCASE("point budget exceeded -> expansion stops")
  {
    auto root = make_nid(1, 0, 0);
    auto child_a = make_nid(1, 1, 0);
    auto child_b = make_nid(1, 1, 1);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {
      make_sub(root, empty, 10, 5000),
      make_sub(child_a, root, 9, 8000),
      make_sub(child_b, root, 9, 8000),
    };
    std::vector<std::pair<node_id_t, node_id_t>> edges = {
      {root, child_a},
      {root, child_b},
    };
    setup_registry(registry, subsets, edges, {true, true, true}, {1024, 2048, 2048});

    selection_params_t params;
    params.camera_position = {0.5, 0.5, 0.5};
    params.memory_budget = 1024 * 1024;
    params.point_budget = 10000; // budget too low for children (16000 total)

    auto result = selector.select(registry, params);
    // Root stays because children would exceed point budget
    REQUIRE(result.active_set.count(root));
  }

  SUBCASE("memory budget exceeded -> expansion stops")
  {
    auto root = make_nid(1, 0, 0);
    auto child_a = make_nid(1, 1, 0);
    auto child_b = make_nid(1, 1, 1);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {
      make_sub(root, empty, 10, 5000),
      make_sub(child_a, root, 9, 3000),
      make_sub(child_b, root, 9, 3000),
    };
    std::vector<std::pair<node_id_t, node_id_t>> edges = {
      {root, child_a},
      {root, child_b},
    };
    setup_registry(registry, subsets, edges, {true, true, true}, {1024, 2048, 2048});

    selection_params_t params;
    params.camera_position = {0.5, 0.5, 0.5};
    params.memory_budget = 2048; // budget too low for children
    params.point_budget = 100000;

    auto result = selector.select(registry, params);
    // Root stays because children memory exceeds budget
    REQUIRE(result.active_set.count(root));
  }

  SUBCASE("distance priority - closer nodes expanded first")
  {
    auto root = make_nid(1, 0, 0);
    auto close_child = make_nid(1, 1, 0);
    auto far_child = make_nid(1, 1, 1);
    node_id_t empty = {};

    // close_child has tight_aabb near camera, far_child is far away
    auto close_sub = make_sub(close_child, root, 9, 3000);
    close_sub.tight_aabb = {{0, 0, 0}, {1, 1, 1}};

    auto far_sub = make_sub(far_child, root, 9, 3000);
    far_sub.tight_aabb = {{100, 100, 100}, {101, 101, 101}};

    auto root_sub = make_sub(root, empty, 10, 5000);
    root_sub.tight_aabb = {{0, 0, 0}, {101, 101, 101}};

    std::vector<tree_walker_data_t> subsets = {root_sub, close_sub, far_sub};
    std::vector<std::pair<node_id_t, node_id_t>> edges = {
      {root, close_child},
      {root, far_child},
    };
    setup_registry(registry, subsets, edges, {true, true, true}, {1024, 512, 512});

    selection_params_t params;
    params.camera_position = {0.5, 0.5, 0.5}; // close to close_child
    params.memory_budget = 1024 * 1024;
    params.point_budget = 100000;

    auto result = selector.select(registry, params);
    // Both children should be expanded (budget allows)
    REQUIRE(result.active_set.count(close_child));
    REQUIRE(result.active_set.count(far_child));
  }
}
