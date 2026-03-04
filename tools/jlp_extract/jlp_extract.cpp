#include <fmt/format.h>

#include <vio/event_loop.h>
#include <vio/operation/file.h>
#include <vio/task.h>

#include <conversion_types.hpp>
#include <attributes_configs.hpp>
#include <tree.hpp>
#include <compressor.hpp>
#include <input_header.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace points;
using namespace points::converter;

struct extract_args_t
{
  std::string input_path;
  std::string attribute_name;
  std::string output_path;
  enum class mode_t { list, index, range, random, summary, trees, tree_detail, node_detail } mode = mode_t::list;
  uint32_t index = 0;
  uint32_t range_begin = 0;
  uint32_t range_end = 0;
  uint32_t random_count = 0;
  uint32_t seed = 0;
  bool has_seed = false;
  uint32_t print_offset = 0;
  uint32_t print_count = 256;
  bool write_binary = false;
  uint32_t tree_id = 0;
  uint32_t node_tree = 0;
  uint32_t node_level = 0;
  uint32_t node_index = 0;
};

static const char *type_name(type_t type)
{
  switch (type)
  {
  case type_u8:   return "u8";
  case type_i8:   return "i8";
  case type_u16:  return "u16";
  case type_i16:  return "i16";
  case type_u32:  return "u32";
  case type_i32:  return "i32";
  case type_m32:  return "m32";
  case type_r32:  return "r32";
  case type_u64:  return "u64";
  case type_i64:  return "i64";
  case type_m64:  return "m64";
  case type_r64:  return "r64";
  case type_m128: return "m128";
  case type_m192: return "m192";
  default:        return "?";
  }
}

static bool parse_args(int argc, char **argv, extract_args_t &args)
{
  if (argc < 2)
  {
    fmt::print(stderr, "Usage: jlp_extract <file.jlp> [<attribute>] [options]\n\n");
    fmt::print(stderr, "Tree introspection:\n");
    fmt::print(stderr, "  --summary        High-level overview: root tree ID, tree config, tree count\n");
    fmt::print(stderr, "  --trees          List all trees with per-level node counts and sub-tree refs\n");
    fmt::print(stderr, "  --tree N         Full dump of tree N: every node, subsets, storage map\n");
    fmt::print(stderr, "  --node N:L:I     Single node detail (tree N, level L, index I)\n\n");
    fmt::print(stderr, "Attribute extraction:\n");
    fmt::print(stderr, "  --index N        Select buffer at index N (default: 0 for hex mode)\n");
    fmt::print(stderr, "  --range A-B      Select buffers A through B (inclusive)\n");
    fmt::print(stderr, "  --random K       Select K random buffers\n");
    fmt::print(stderr, "  --seed S         Seed for random selection\n");
    fmt::print(stderr, "  --offset N       Start printing from element N (default: 0)\n");
    fmt::print(stderr, "  --count N / -n N Number of elements to print (default: 256)\n");
    fmt::print(stderr, "  -o <path>        Write binary output to file (without: hex dump to stdout)\n");
    return false;
  }

  args.input_path = argv[1];

  int i = 2;
  if (i < argc && argv[i][0] != '-')
  {
    args.attribute_name = argv[i];
    i++;
  }

  while (i < argc)
  {
    std::string arg = argv[i];
    if (arg == "--index" && i + 1 < argc)
    {
      args.mode = extract_args_t::mode_t::index;
      args.index = uint32_t(std::stoul(argv[++i]));
    }
    else if (arg == "--range" && i + 1 < argc)
    {
      args.mode = extract_args_t::mode_t::range;
      std::string range_str = argv[++i];
      auto dash = range_str.find('-');
      if (dash == std::string::npos)
      {
        fmt::print(stderr, "Error: --range requires A-B format\n");
        return false;
      }
      args.range_begin = uint32_t(std::stoul(range_str.substr(0, dash)));
      args.range_end = uint32_t(std::stoul(range_str.substr(dash + 1)));
    }
    else if (arg == "--random" && i + 1 < argc)
    {
      args.mode = extract_args_t::mode_t::random;
      args.random_count = uint32_t(std::stoul(argv[++i]));
    }
    else if (arg == "--seed" && i + 1 < argc)
    {
      args.seed = uint32_t(std::stoul(argv[++i]));
      args.has_seed = true;
    }
    else if (arg == "-o" && i + 1 < argc)
    {
      args.output_path = argv[++i];
      args.write_binary = true;
    }
    else if (arg == "--offset" && i + 1 < argc)
    {
      args.print_offset = uint32_t(std::stoul(argv[++i]));
    }
    else if ((arg == "--count" || arg == "-n") && i + 1 < argc)
    {
      args.print_count = uint32_t(std::stoul(argv[++i]));
    }
    else if (arg == "--summary")
    {
      args.mode = extract_args_t::mode_t::summary;
    }
    else if (arg == "--trees")
    {
      args.mode = extract_args_t::mode_t::trees;
    }
    else if (arg == "--tree" && i + 1 < argc)
    {
      args.mode = extract_args_t::mode_t::tree_detail;
      args.tree_id = uint32_t(std::stoul(argv[++i]));
    }
    else if (arg == "--node" && i + 1 < argc)
    {
      args.mode = extract_args_t::mode_t::node_detail;
      std::string node_str = argv[++i];
      auto first_colon = node_str.find(':');
      auto second_colon = first_colon != std::string::npos ? node_str.find(':', first_colon + 1) : std::string::npos;
      if (first_colon == std::string::npos || second_colon == std::string::npos)
      {
        fmt::print(stderr, "Error: --node requires N:L:I format (e.g. 0:1:2)\n");
        return false;
      }
      args.node_tree = uint32_t(std::stoul(node_str.substr(0, first_colon)));
      args.node_level = uint32_t(std::stoul(node_str.substr(first_colon + 1, second_colon - first_colon - 1)));
      args.node_index = uint32_t(std::stoul(node_str.substr(second_colon + 1)));
    }
    else
    {
      fmt::print(stderr, "Error: unknown option '{}'\n", arg);
      return false;
    }
    i++;
  }

  // When attribute is given but no mode specified, default to index 0
  if (!args.attribute_name.empty() && args.mode == extract_args_t::mode_t::list)
  {
    args.mode = extract_args_t::mode_t::index;
    args.index = 0;
  }

  return true;
}

static void print_hex_values(const uint8_t *data, uint32_t data_size,
                             point_format_t format, uint32_t offset, uint32_t count,
                             uint32_t buffer_index)
{
  int type_size = size_for_format(format.type);
  int element_size = type_size * int(format.components);
  if (element_size <= 0)
  {
    fmt::print(stderr, "Error: unknown element size for format\n");
    return;
  }
  uint32_t total_elements = data_size / uint32_t(element_size);
  if (offset >= total_elements)
  {
    fmt::print("No elements in range (total: {})\n", total_elements);
    return;
  }
  uint32_t avail = total_elements - offset;
  if (count > avail)
    count = avail;

  fmt::print("Format: {}x{}, buffer {}, elements {}-{} of {}\n",
             type_name(format.type), int(format.components),
             buffer_index, offset, offset + count - 1, total_elements);

  // Determine index column width
  int idx_width = 1;
  {
    uint32_t max_idx = offset + count - 1;
    while (max_idx >= 10) { max_idx /= 10; idx_width++; }
  }

  for (uint32_t i = 0; i < count; i++)
  {
    uint32_t elem_idx = offset + i;
    const uint8_t *elem = data + uint32_t(element_size) * elem_idx;
    fmt::print("{:>{}}: ", elem_idx, idx_width);
    for (int c = 0; c < int(format.components); c++)
    {
      if (c > 0)
        fmt::print(" ");
      const uint8_t *comp = elem + type_size * c;
      // Print bytes high-to-low for readability (big-endian display)
      for (int b = type_size - 1; b >= 0; b--)
        fmt::print("{:02x}", comp[b]);
    }
    fmt::print("\n");
  }
}

static std::string morton_to_hex(const morton::morton192_t &m)
{
  if (m.data[2])
    return fmt::format("{:x}-{:016x}-{:016x}", m.data[2], m.data[1], m.data[0]);
  if (m.data[1])
    return fmt::format("{:x}-{:016x}", m.data[1], m.data[0]);
  return fmt::format("{:x}", m.data[0]);
}

static int count_children(uint8_t bitmask)
{
  int count = 0;
  for (uint8_t v = bitmask; v; v >>= 1)
    count += v & 1;
  return count;
}

static vio::task_t<bool> load_tree(vio::event_loop_t &event_loop, vio::file_t &file,
                                    const storage_location_t &loc, tree_t &tree)
{
  if (loc.size == 0)
    co_return false;

  auto blob = std::make_shared<uint8_t[]>(loc.size);
  auto rd = co_await vio::read_file(event_loop, file, blob.get(), loc.size, int64_t(loc.offset));
  if (!rd.has_value() || rd.value() != loc.size)
    co_return false;

  serialized_tree_t st;
  st.data = std::move(blob);
  st.size = int(loc.size);

  points::error_t err;
  co_return tree_deserialize(st, tree, err);
}

static void print_tree_registry_header(const tree_registry_t &reg)
{
  fmt::print("Tree registry: root={}, trees={}, node_limit={}, scale={}, offset=[{}, {}, {}]\n",
             reg.root.data, reg.locations.size(), reg.node_limit,
             reg.tree_config.scale,
             reg.tree_config.offset[0], reg.tree_config.offset[1], reg.tree_config.offset[2]);
}

static void print_tree_summary(uint32_t tree_idx, const storage_location_t &loc, const tree_t &tree)
{
  fmt::print("Tree {}: magnitude={}, morton=[{}]-[{}], storage: offset={} size={}\n",
             tree_idx, int(tree.magnitude),
             morton_to_hex(tree.morton_min), morton_to_hex(tree.morton_max),
             loc.offset, loc.size);

  for (int level = 0; level < 5; level++)
  {
    auto node_count = uint32_t(tree.nodes[level].size());
    if (node_count == 0)
      continue;
    uint32_t with_children = 0;
    for (auto n : tree.nodes[level])
      if (n != 0)
        with_children++;
    auto lod = morton::morton_tree_level_to_lod(int(tree.magnitude), level);
    fmt::print("  Level {} (lod {}): {} node{}, {} with children\n",
               level, lod, node_count, node_count == 1 ? "" : "s", with_children);
  }

  if (!tree.sub_trees.empty())
  {
    fmt::print("  Sub-trees: {} [", tree.sub_trees.size());
    for (size_t i = 0; i < tree.sub_trees.size(); i++)
    {
      if (i > 0)
        fmt::print(", ");
      fmt::print("tree {}", tree.sub_trees[i].data);
    }
    fmt::print("]\n");
  }
}

static void print_tree_detail(uint32_t tree_idx, const tree_t &tree)
{
  fmt::print("Tree {}: magnitude={}, morton=[{}]-[{}]\n",
             tree_idx, int(tree.magnitude),
             morton_to_hex(tree.morton_min), morton_to_hex(tree.morton_max));

  for (int level = 0; level < 5; level++)
  {
    auto node_count = uint32_t(tree.nodes[level].size());
    if (node_count == 0)
      continue;
    auto lod = morton::morton_tree_level_to_lod(int(tree.magnitude), level);
    fmt::print("  Level {} (lod {}):\n", level, lod);

    for (uint32_t ni = 0; ni < node_count; ni++)
    {
      uint8_t children = tree.nodes[level][ni];
      uint16_t node_id = tree.node_ids[level][ni];
      auto &pc = tree.data[level][ni];
      int child_count = count_children(children);

      fmt::print("    [{}] node_id=0x{:04x}, children=0x{:02x} ({}/8), points: {}",
                 ni, node_id, children, child_count, pc.point_count);
      if (!pc.data.empty())
        fmt::print(" ({} subset{})", pc.data.size(), pc.data.size() == 1 ? "" : "s");
      fmt::print("\n");

      for (size_t si = 0; si < pc.data.size(); si++)
      {
        auto &sub = pc.data[si];
        fmt::print("        subset {}: input_id={}.{}, offset={}, count={}{}\n",
                   si, sub.input_id.data, sub.input_id.sub,
                   sub.offset.data, sub.count.data,
                   input_data_id_is_leaf(sub.input_id) ? "" : " [lod]");
      }
    }
  }

  if (!tree.sub_trees.empty())
  {
    fmt::print("  Sub-trees: {} [", tree.sub_trees.size());
    for (size_t i = 0; i < tree.sub_trees.size(); i++)
    {
      if (i > 0)
        fmt::print(", ");
      fmt::print("tree {}", tree.sub_trees[i].data);
    }
    fmt::print("]\n");
  }

  uint32_t map_count = 0;
  tree.storage_map.for_each([&](input_data_id_t, attributes_id_t, const std::vector<storage_location_t> &) {
    map_count++;
  });

  if (map_count > 0)
  {
    fmt::print("  Storage map ({} entries):\n", map_count);
    tree.storage_map.for_each([&](input_data_id_t id, attributes_id_t attrib_id, const std::vector<storage_location_t> &storage) {
      fmt::print("    input_id={}.{}, attrib_id={}, locations: {}\n",
                 id.data, id.sub, attrib_id.data, storage.size());
      for (size_t i = 0; i < storage.size(); i++)
      {
        fmt::print("      [{}] offset={}, size={}, file_id={}\n",
                   i, storage[i].offset, storage[i].size, storage[i].file_id);
      }
    });
  }
}

static void print_node_detail(uint32_t tree_idx, const tree_t &tree, uint32_t level, uint32_t index)
{
  auto lod = morton::morton_tree_level_to_lod(int(tree.magnitude), int(level));
  fmt::print("Tree {}, Level {} (lod {}), Node [{}]:\n", tree_idx, level, lod, index);

  uint8_t children = tree.nodes[level][index];
  uint16_t node_id = tree.node_ids[level][index];
  auto &pc = tree.data[level][index];
  int child_count = count_children(children);

  fmt::print("  node_id: 0x{:04x}\n", node_id);
  fmt::print("  children bitmask: 0x{:02x} ({}/8)\n", children, child_count);
  fmt::print("  point_count: {}\n", pc.point_count);
  fmt::print("  morton_min: [{}]\n", morton_to_hex(pc.min));
  fmt::print("  morton_max: [{}]\n", morton_to_hex(pc.max));

  if (!pc.data.empty())
  {
    fmt::print("  subsets ({}):\n", pc.data.size());
    for (size_t si = 0; si < pc.data.size(); si++)
    {
      auto &sub = pc.data[si];
      fmt::print("    [{}] input_id={}.{}, offset={}, count={}{}\n",
                 si, sub.input_id.data, sub.input_id.sub,
                 sub.offset.data, sub.count.data,
                 input_data_id_is_leaf(sub.input_id) ? "" : " [lod]");

      auto [attrib_id, storage] = tree.storage_map.info(sub.input_id);
      fmt::print("         attrib_id={}, storage locations: {}\n", attrib_id.data, storage.size());
      for (size_t li = 0; li < storage.size(); li++)
      {
        fmt::print("           [{}] offset={}, size={}, file_id={}\n",
                   li, storage[li].offset, storage[li].size, storage[li].file_id);
      }
    }
  }
}

static vio::task_t<void> run_extract(vio::event_loop_t &event_loop, extract_args_t args, int &exit_code)
{
  // Open JLP file
  auto opened_file = vio::open_file(event_loop, args.input_path, vio::file_open_flag_t::rdonly, 0);
  if (!opened_file.has_value())
  {
    fmt::print(stderr, "Error: failed to open '{}': {}\n", args.input_path, opened_file.error().msg);
    exit_code = 1;
    event_loop.stop();
    co_return;
  }
  auto jlp_file = std::move(opened_file.value());

  // Read 128-byte index
  uint8_t index_buf[128];
  memset(index_buf, 0, sizeof(index_buf));
  auto read_result = co_await vio::read_file(event_loop, *jlp_file, index_buf, 128, 0);
  if (!read_result.has_value() || read_result.value() != 128)
  {
    fmt::print(stderr, "Error: failed to read JLP index\n");
    exit_code = 1;
    event_loop.stop();
    co_return;
  }

  // Check magic
  if (index_buf[0] != 'J' || index_buf[1] != 'L' || index_buf[2] != 'P' || index_buf[3] != 0)
  {
    fmt::print(stderr, "Error: '{}' is not a JLP file\n", args.input_path);
    exit_code = 1;
    event_loop.stop();
    co_return;
  }

  // Parse storage locations from index
  storage_location_t attrib_configs_loc;
  storage_location_t tree_registry_loc;
  memcpy(&attrib_configs_loc, index_buf + 4 + sizeof(storage_location_t), sizeof(storage_location_t));
  memcpy(&tree_registry_loc, index_buf + 4 + 2 * sizeof(storage_location_t), sizeof(storage_location_t));

  if (attrib_configs_loc.size == 0)
  {
    fmt::print(stderr, "Error: no attribute configs in this file\n");
    exit_code = 1;
    event_loop.stop();
    co_return;
  }

  // Read attribute configs blob
  auto attrib_blob = std::make_unique<uint8_t[]>(attrib_configs_loc.size);
  auto attrib_read = co_await vio::read_file(event_loop, *jlp_file, attrib_blob.get(), attrib_configs_loc.size, int64_t(attrib_configs_loc.offset));
  if (!attrib_read.has_value() || attrib_read.value() != attrib_configs_loc.size)
  {
    fmt::print(stderr, "Error: failed to read attribute configs\n");
    exit_code = 1;
    event_loop.stop();
    co_return;
  }

  attributes_configs_t attrib_configs;
  auto attrib_err = attrib_configs.deserialize(attrib_blob, attrib_configs_loc.size);
  if (attrib_err.code != 0)
  {
    fmt::print(stderr, "Error: failed to deserialize attribute configs: {}\n", attrib_err.msg);
    exit_code = 1;
    event_loop.stop();
    co_return;
  }

  // Read tree registry blob (if available)
  tree_registry_t tree_registry;
  bool has_tree_registry = false;
  if (tree_registry_loc.size > 0)
  {
    auto tree_reg_blob = std::make_unique<uint8_t[]>(tree_registry_loc.size);
    auto tree_reg_read = co_await vio::read_file(event_loop, *jlp_file, tree_reg_blob.get(), tree_registry_loc.size, int64_t(tree_registry_loc.offset));
    if (!tree_reg_read.has_value() || tree_reg_read.value() != tree_registry_loc.size)
    {
      fmt::print(stderr, "Error: failed to read tree registry\n");
      exit_code = 1;
      event_loop.stop();
      co_return;
    }

    auto tree_reg_err = tree_registry_deserialize(tree_reg_blob, tree_registry_loc.size, tree_registry);
    if (tree_reg_err.code != 0)
    {
      fmt::print(stderr, "Error: failed to deserialize tree registry: {}\n", tree_reg_err.msg);
      exit_code = 1;
      event_loop.stop();
      co_return;
    }
    has_tree_registry = true;
  }

  // Tree introspection modes
  bool is_tree_mode = args.mode == extract_args_t::mode_t::summary
                   || args.mode == extract_args_t::mode_t::trees
                   || args.mode == extract_args_t::mode_t::tree_detail
                   || args.mode == extract_args_t::mode_t::node_detail;

  if (is_tree_mode)
  {
    if (!has_tree_registry)
    {
      fmt::print(stderr, "Error: no tree registry in this file\n");
      exit_code = 1;
      event_loop.stop();
      co_return;
    }

    if (args.mode == extract_args_t::mode_t::summary)
    {
      print_tree_registry_header(tree_registry);
      uint32_t trees_with_data = 0;
      for (auto &loc : tree_registry.locations)
        if (loc.size > 0)
          trees_with_data++;
      fmt::print("Total trees with data: {} / {}\n", trees_with_data, tree_registry.locations.size());
    }
    else if (args.mode == extract_args_t::mode_t::trees)
    {
      print_tree_registry_header(tree_registry);
      fmt::print("\n");
      for (uint32_t ti = 0; ti < uint32_t(tree_registry.locations.size()); ti++)
      {
        auto &loc = tree_registry.locations[ti];
        if (loc.size == 0)
          continue;

        tree_t tree;
        if (!co_await load_tree(event_loop, *jlp_file, loc, tree))
        {
          fmt::print(stderr, "Warning: failed to load tree {}, skipping\n", ti);
          continue;
        }
        print_tree_summary(ti, loc, tree);
        fmt::print("\n");
      }
    }
    else if (args.mode == extract_args_t::mode_t::tree_detail)
    {
      if (args.tree_id >= uint32_t(tree_registry.locations.size()))
      {
        fmt::print(stderr, "Error: tree {} out of range (0-{})\n", args.tree_id, tree_registry.locations.size() - 1);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }
      auto &loc = tree_registry.locations[args.tree_id];
      if (loc.size == 0)
      {
        fmt::print(stderr, "Error: tree {} has no data\n", args.tree_id);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }

      tree_t tree;
      if (!co_await load_tree(event_loop, *jlp_file, loc, tree))
      {
        fmt::print(stderr, "Error: failed to load tree {}\n", args.tree_id);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }
      print_tree_detail(args.tree_id, tree);
    }
    else if (args.mode == extract_args_t::mode_t::node_detail)
    {
      if (args.node_tree >= uint32_t(tree_registry.locations.size()))
      {
        fmt::print(stderr, "Error: tree {} out of range (0-{})\n", args.node_tree, tree_registry.locations.size() - 1);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }
      auto &loc = tree_registry.locations[args.node_tree];
      if (loc.size == 0)
      {
        fmt::print(stderr, "Error: tree {} has no data\n", args.node_tree);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }

      tree_t tree;
      if (!co_await load_tree(event_loop, *jlp_file, loc, tree))
      {
        fmt::print(stderr, "Error: failed to load tree {}\n", args.node_tree);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }

      if (args.node_level > 4)
      {
        fmt::print(stderr, "Error: level {} out of range (0-4)\n", args.node_level);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }
      if (args.node_index >= uint32_t(tree.nodes[args.node_level].size()))
      {
        fmt::print(stderr, "Error: node index {} out of range for level {} (0-{})\n",
                   args.node_index, args.node_level, tree.nodes[args.node_level].size() - 1);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }

      print_node_detail(args.node_tree, tree, args.node_level, args.node_index);
    }

    event_loop.stop();
    co_return;
  }

  // List mode: print available attributes and exit
  if (args.attribute_name.empty())
  {
    auto name_count = attrib_configs.attrib_name_registry_count();
    fmt::print("Available attributes ({}):\n", name_count);
    for (uint32_t i = 0; i < name_count; i++)
    {
      char name_buf[256];
      attrib_configs.attrib_name_registry_get(i, name_buf, sizeof(name_buf));
      fmt::print("  {}\n", name_buf);
    }
    event_loop.stop();
    co_return;
  }

  if (!has_tree_registry)
  {
    fmt::print(stderr, "Error: no tree registry in this file\n");
    exit_code = 1;
    event_loop.stop();
    co_return;
  }

  // Pass 1: collect all matching buffer locations by reading each tree
  struct buffer_entry_t
  {
    storage_location_t location;
    point_format_t format;
  };
  std::vector<buffer_entry_t> all_buffers;

  for (uint32_t ti = 0; ti < uint32_t(tree_registry.locations.size()); ti++)
  {
    auto &tree_loc = tree_registry.locations[ti];
    if (tree_loc.size == 0)
      continue;

    auto tree_blob_data = std::make_shared<uint8_t[]>(tree_loc.size);
    auto tree_read = co_await vio::read_file(event_loop, *jlp_file, tree_blob_data.get(), tree_loc.size, int64_t(tree_loc.offset));
    if (!tree_read.has_value() || tree_read.value() != tree_loc.size)
    {
      fmt::print(stderr, "Warning: failed to read tree {}, skipping\n", ti);
      continue;
    }

    serialized_tree_t serialized_tree;
    serialized_tree.data = std::move(tree_blob_data);
    serialized_tree.size = int(tree_loc.size);

    tree_t tree;
    points::error_t tree_err;
    if (!tree_deserialize(serialized_tree, tree, tree_err))
    {
      fmt::print(stderr, "Warning: failed to deserialize tree {}: {}, skipping\n", ti, tree_err.msg);
      continue;
    }

    tree.storage_map.for_each([&](input_data_id_t id, attributes_id_t attrib_id, const std::vector<storage_location_t> &storage) {
      (void)id;
      auto idx = attrib_configs.get_attribute_index(attrib_id, args.attribute_name);
      if (idx.index < 0)
        return;
      if (idx.index < int(storage.size()))
      {
        all_buffers.push_back({storage[uint32_t(idx.index)], idx.format});
      }
    });
  }

  if (all_buffers.empty())
  {
    fmt::print(stderr, "Error: no buffers found for attribute '{}'\n", args.attribute_name);
    exit_code = 1;
    event_loop.stop();
    co_return;
  }

  fmt::print("Found {} buffers for attribute '{}'\n", all_buffers.size(), args.attribute_name);

  // Apply selection
  std::vector<buffer_entry_t> selected;
  switch (args.mode)
  {
  case extract_args_t::mode_t::index:
    if (args.index >= all_buffers.size())
    {
      fmt::print(stderr, "Error: index {} out of range (0-{})\n", args.index, all_buffers.size() - 1);
      exit_code = 1;
      event_loop.stop();
      co_return;
    }
    selected.push_back(all_buffers[args.index]);
    break;
  case extract_args_t::mode_t::range:
    if (args.range_end >= all_buffers.size())
    {
      fmt::print(stderr, "Error: range end {} out of range (max {})\n", args.range_end, all_buffers.size() - 1);
      exit_code = 1;
      event_loop.stop();
      co_return;
    }
    for (uint32_t i = args.range_begin; i <= args.range_end; i++)
      selected.push_back(all_buffers[i]);
    break;
  case extract_args_t::mode_t::random:
  {
    auto count = std::min(args.random_count, uint32_t(all_buffers.size()));
    std::vector<uint32_t> indices(all_buffers.size());
    for (uint32_t i = 0; i < uint32_t(indices.size()); i++)
      indices[i] = i;
    std::mt19937 rng(args.has_seed ? args.seed : std::random_device{}());
    for (uint32_t i = 0; i < count; i++)
    {
      auto j = std::uniform_int_distribution<uint32_t>(i, uint32_t(indices.size()) - 1)(rng);
      std::swap(indices[i], indices[j]);
    }
    for (uint32_t i = 0; i < count; i++)
      selected.push_back(all_buffers[indices[i]]);
    break;
  }
  default:
    break;
  }

  if (selected.empty())
  {
    fmt::print("No buffers selected.\n");
    event_loop.stop();
    co_return;
  }

  if (args.write_binary)
  {
    // Binary output mode: write decompressed data to file
    fmt::print("Extracting {} buffer(s) to '{}'\n", selected.size(), args.output_path);

    auto out_opened = vio::open_file(event_loop, args.output_path,
      {vio::file_open_flag_t::wronly, vio::file_open_flag_t::creat, vio::file_open_flag_t::trunc}, 0644);
    if (!out_opened.has_value())
    {
      fmt::print(stderr, "Error: failed to open output '{}': {}\n", args.output_path, out_opened.error().msg);
      exit_code = 1;
      event_loop.stop();
      co_return;
    }
    auto out_file = std::move(out_opened.value());

    int64_t write_offset = 0;
    uint64_t total_bytes = 0;
    uint32_t buffers_written = 0;
    point_format_t buffer_format = selected[0].format;

    for (auto &entry : selected)
    {
      if (entry.location.size == 0)
        continue;

      auto buf = std::make_unique<uint8_t[]>(entry.location.size);
      auto rd = co_await vio::read_file(event_loop, *jlp_file, buf.get(), entry.location.size, int64_t(entry.location.offset));
      if (!rd.has_value() || rd.value() != entry.location.size)
      {
        fmt::print(stderr, "Warning: failed to read buffer at offset {}, skipping\n", entry.location.offset);
        continue;
      }

      const uint8_t *write_data = buf.get();
      uint32_t write_size = entry.location.size;

      compression_result_t decompressed;
      if (has_compression_magic(buf.get(), entry.location.size))
      {
        decompressed = decompress_any(buf.get(), entry.location.size);
        if (decompressed.error.code != 0)
        {
          fmt::print(stderr, "Warning: decompression failed: {}, skipping\n", decompressed.error.msg);
          continue;
        }
        write_data = decompressed.data.get();
        write_size = decompressed.size;
      }

      auto wr = co_await vio::write_file(event_loop, *out_file, write_data, write_size, write_offset);
      if (!wr.has_value())
      {
        fmt::print(stderr, "Error: write failed: {}\n", wr.error().msg);
        exit_code = 1;
        event_loop.stop();
        co_return;
      }
      write_offset += int64_t(write_size);
      total_bytes += write_size;
      buffers_written++;
    }

    fmt::print("Done: {} buffers, {} bytes, format {}x{}\n",
               buffers_written, total_bytes,
               type_name(buffer_format.type), int(buffer_format.components));
  }
  else
  {
    // Hex dump mode: print decompressed values to stdout
    uint32_t buffer_idx = 0;
    for (auto &entry : selected)
    {
      if (entry.location.size == 0)
      {
        buffer_idx++;
        continue;
      }

      auto buf = std::make_unique<uint8_t[]>(entry.location.size);
      auto rd = co_await vio::read_file(event_loop, *jlp_file, buf.get(), entry.location.size, int64_t(entry.location.offset));
      if (!rd.has_value() || rd.value() != entry.location.size)
      {
        fmt::print(stderr, "Warning: failed to read buffer at offset {}, skipping\n", entry.location.offset);
        buffer_idx++;
        continue;
      }

      const uint8_t *out_data = buf.get();
      uint32_t out_size = entry.location.size;

      compression_result_t decompressed;
      if (has_compression_magic(buf.get(), entry.location.size))
      {
        decompressed = decompress_any(buf.get(), entry.location.size);
        if (decompressed.error.code != 0)
        {
          fmt::print(stderr, "Warning: decompression failed: {}, skipping\n", decompressed.error.msg);
          buffer_idx++;
          continue;
        }
        out_data = decompressed.data.get();
        out_size = decompressed.size;
      }

      // Determine the actual buffer index for display
      uint32_t display_idx = args.index;
      if (args.mode == extract_args_t::mode_t::range)
        display_idx = args.range_begin + buffer_idx;
      else if (args.mode == extract_args_t::mode_t::random)
        display_idx = buffer_idx;

      print_hex_values(out_data, out_size, entry.format, args.print_offset, args.print_count, display_idx);

      if (buffer_idx + 1 < uint32_t(selected.size()))
        fmt::print("\n");
      buffer_idx++;
    }
  }

  event_loop.stop();
  co_return;
}

int main(int argc, char **argv)
{
  extract_args_t args;
  if (!parse_args(argc, argv, args))
    return 1;

  int exit_code = 0;
  vio::event_loop_t event_loop;
  std::optional<vio::task_t<void>> task;
  event_loop.run_in_loop([&] {
    task.emplace(
      [](vio::event_loop_t &el, extract_args_t a, int &ec) -> vio::task_t<void> {
        co_await run_extract(el, std::move(a), ec);
      }(event_loop, std::move(args), exit_code));
  });
  event_loop.run();

  return exit_code;
}
