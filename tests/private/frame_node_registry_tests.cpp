#include <doctest/doctest.h>
#include <frame_node_registry.hpp>
#include <gpu_node_buffer.hpp>

using namespace points;
using namespace points::converter;

static node_id_t make_node_id(uint32_t tree, uint16_t level, uint16_t index)
{
  node_id_t id;
  id.tree_id.data = tree;
  id.level = level;
  id.index = index;
  return id;
}

static tree_walker_data_t make_subset(node_id_t node, node_id_t parent, int lod, uint32_t point_count = 1000)
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

TEST_CASE("frame_node_registry basic operations")
{
  frame_node_registry_t registry;
  REQUIRE(registry.empty());

  SUBCASE("first frame - all nodes added")
  {
    auto root_id = make_node_id(1, 0, 0);
    auto child_a = make_node_id(1, 1, 0);
    auto child_b = make_node_id(1, 1, 1);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {
      make_subset(root_id, empty, 10),
      make_subset(child_a, root_id, 9),
      make_subset(child_b, root_id, 9),
    };

    // Create matching render_buffers
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    for (auto &s : subsets)
    {
      auto b = std::make_unique<gpu_node_buffer_t>();
      b->node_info = s;
      bufs.push_back(std::move(b));
    }

    std::vector<std::pair<node_id_t, node_id_t>> edges = {
      {root_id, child_a},
      {root_id, child_b},
    };

    auto diff = registry.update_from_walker(subsets, edges, bufs);

    REQUIRE(!registry.empty());
    REQUIRE(diff.added.size() == 3);
    REQUIRE(diff.removed.empty());

    auto *root_node = registry.get_node(root_id);
    REQUIRE(root_node != nullptr);
    REQUIRE(root_node->children.size() == 2);

    REQUIRE(registry.roots().size() == 1);
    REQUIRE((registry.roots()[0] <=> root_id) == std::strong_ordering::equal);
  }

  SUBCASE("stable frame - empty diff for existing nodes")
  {
    auto root_id = make_node_id(1, 0, 0);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {make_subset(root_id, empty, 10)};

    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = subsets[0];
    bufs.push_back(std::move(b));

    std::vector<std::pair<node_id_t, node_id_t>> edges;

    // First frame
    registry.update_from_walker(subsets, edges, bufs);

    // Second frame with same data
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs2;
    auto b2 = std::make_unique<gpu_node_buffer_t>();
    b2->node_info = subsets[0];
    bufs2.push_back(std::move(b2));

    auto diff = registry.update_from_walker(subsets, edges, bufs2);

    REQUIRE(diff.removed.empty());
    // Root already existed, so it should be in updated, not added
    REQUIRE(registry.get_node(root_id) != nullptr);
  }

  SUBCASE("node removal")
  {
    auto root_id = make_node_id(1, 0, 0);
    auto child_id = make_node_id(1, 1, 0);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {
      make_subset(root_id, empty, 10),
      make_subset(child_id, root_id, 9),
    };

    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    for (auto &s : subsets)
    {
      auto b = std::make_unique<gpu_node_buffer_t>();
      b->node_info = s;
      bufs.push_back(std::move(b));
    }

    std::vector<std::pair<node_id_t, node_id_t>> edges = {{root_id, child_id}};
    registry.update_from_walker(subsets, edges, bufs);
    REQUIRE(registry.get_node(child_id) != nullptr);

    // Remove child from walker output
    std::vector<tree_walker_data_t> subsets2 = {make_subset(root_id, empty, 10)};
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs2;
    auto b2 = std::make_unique<gpu_node_buffer_t>();
    b2->node_info = subsets2[0];
    bufs2.push_back(std::move(b2));

    auto diff = registry.update_from_walker(subsets2, {}, bufs2);
    REQUIRE(diff.removed.size() == 1);
    REQUIRE(registry.get_node(child_id) == nullptr);
  }

  SUBCASE("topology from edges")
  {
    auto root_id = make_node_id(1, 0, 0);
    auto child_a = make_node_id(1, 1, 0);
    auto child_b = make_node_id(1, 1, 1);
    auto grandchild = make_node_id(1, 2, 0);
    node_id_t empty = {};

    std::vector<tree_walker_data_t> subsets = {
      make_subset(root_id, empty, 10),
      make_subset(child_a, root_id, 9),
      make_subset(child_b, root_id, 9),
      make_subset(grandchild, child_a, 8),
    };

    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    for (auto &s : subsets)
    {
      auto b = std::make_unique<gpu_node_buffer_t>();
      b->node_info = s;
      bufs.push_back(std::move(b));
    }

    std::vector<std::pair<node_id_t, node_id_t>> edges = {
      {root_id, child_a},
      {root_id, child_b},
      {child_a, grandchild},
    };

    registry.update_from_walker(subsets, edges, bufs);

    auto *root_node = registry.get_node(root_id);
    REQUIRE(root_node->children.size() == 2);

    auto *child_a_node = registry.get_node(child_a);
    REQUIRE(child_a_node->children.size() == 1);

    auto *child_b_node = registry.get_node(child_b);
    REQUIRE(child_b_node->children.empty());
  }
}
