#include "commands.hpp"
#include "tool_common.hpp"

#include <argh.h>
#include <fmt/format.h>
#include <dew/converter/connection_cli.h>
#include <dew/converter/converter.h>

#include "blob_residency.hpp"
#include "bucket_format.hpp"
#include "index_format.hpp"
#include "url.hpp"

#include <ankerl/unordered_dense.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


namespace
{

// ---- cache / destination state sections (private-format introspection) --------------------------

std::string uuid_hex(const uint8_t (&uuid)[16])
{
  std::string out;
  for (auto b : uuid)
    out += fmt::format("{:02x}", b);
  return out;
}

// Local packed cache: superblock extras (dataset uuid) + blob residency table. Classic files carry
// neither and print nothing.
void print_local_cache_state(const std::string &path)
{
  namespace pc = dew::converter;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return;
  uint8_t superblock[pc::k_serialized_index_size];
  if (fread(superblock, 1, sizeof(superblock), f) != sizeof(superblock))
  {
    fclose(f);
    return;
  }
  pc::storage_location_t free_blobs, attrs, registry, stats, perf;
  pc::index_extras_t extras;
  if (pc::deserialize_index(superblock, sizeof(superblock), free_blobs, attrs, registry, stats, perf, &extras).code != 0)
  {
    fclose(f);
    return;
  }
  bool has_uuid = false;
  for (auto b : extras.dataset_uuid)
    has_uuid = has_uuid || b != 0;
  if (!has_uuid && extras.residency_table.size == 0)
  {
    fclose(f);
    return;
  }

  fmt::print("Cache state:\n");
  if (has_uuid)
    fmt::print("  Dataset uuid:  {}\n", uuid_hex(extras.dataset_uuid));
  if (extras.residency_table.size > 0)
  {
    std::vector<uint8_t> table(extras.residency_table.size);
    pc::blob_residency_t residency;
    if (fseek(f, long(extras.residency_table.offset), SEEK_SET) == 0 && fread(table.data(), 1, table.size(), f) == table.size() && residency.deserialize(table.data(), uint32_t(table.size())).code == 0)
    {
      uint64_t count_by_state[5] = {};
      uint64_t bytes_by_state[5] = {};
      ankerl::unordered_dense::map<uint32_t, std::pair<uint64_t, uint64_t>> spill_segments; // seg -> {blobs, bytes}
      residency.for_each([&](const pc::blob_residency_entry_t &e) {
        auto s = size_t(e.state);
        if (s < 5)
        {
          count_by_state[s]++;
          bytes_by_state[s] += e.size;
        }
        if (e.state == pc::blob_residency_state_t::local_spilled || e.state == pc::blob_residency_state_t::remote_spilled)
        {
          auto &seg = spill_segments[uint32_t(e.remote_id >> 32)];
          seg.first++;
          seg.second += e.size;
        }
      });
      fmt::print("  Tracked blobs: {}\n", residency.entry_count());
      auto print_state = [&](size_t idx, const char *name) {
        if (count_by_state[idx])
          fmt::print("    {:<16} {:>6} blobs  {:>10.1f} KB\n", name, count_by_state[idx], double(bytes_by_state[idx]) / 1024.0);
      };
      print_state(size_t(pc::blob_residency_state_t::local_uploaded), "local+uploaded");
      print_state(size_t(pc::blob_residency_state_t::remote_uploaded), "evicted");
      print_state(size_t(pc::blob_residency_state_t::local_spilled), "local+spilled");
      print_state(size_t(pc::blob_residency_state_t::remote_spilled), "spilled");
      if (!spill_segments.empty())
      {
        fmt::print("  Spill segments referenced:\n");
        for (auto &[seg, info] : spill_segments)
          fmt::print("    seg_{:08x}: {} blobs, {:.1f} KB\n", seg, info.first, double(info.second) / 1024.0);
      }
    }
    else
    {
      fmt::print("  (failed to read residency table)\n");
    }
  }
  fmt::print("\n");
  fclose(f);
}

// dir:// DEW2 bucket: the root manifest is a plain file; print its state. (Cloud buckets are opened
// through the converter above; their manifest is not re-fetched here.)
void print_dir_bucket_state(const std::string &dir_path)
{
  namespace pc = dew::converter;
  auto manifest_path = dir_path + "/" + pc::bucket_root_manifest_name();
  FILE *f = fopen(manifest_path.c_str(), "rb");
  if (!f)
    return;
  uint8_t buffer[pc::k_root_manifest_size];
  auto bytes = fread(buffer, 1, sizeof(buffer), f);
  fclose(f);
  pc::root_manifest_t root;
  if (pc::deserialize_root_manifest(buffer, uint32_t(bytes), root).code != 0)
    return;
  fmt::print("DEW2 destination state:\n");
  fmt::print("  Dataset uuid:  {}\n", uuid_hex(root.dataset_uuid));
  fmt::print("  Complete:      {}\n", root.complete ? "yes" : "no (upload in progress or parked)");
  fmt::print("  Bands:         {}\n", root.band_count);
  fmt::print("  Objects:       {}\n", root.next_object_id);
  fmt::print("\n");
}

using tool::converter_handle_t;
using tool::format_bytes;
using tool::format_number;
using tool::method_name;
using tool::print_attribute_table;
using tool::table_row_t;
using tool::type_name;

} // namespace

int cmd_info(int argc, char **argv)
{
  argh::parser cmdl;
  cmdl.parse(argc, argv);
  const auto &files = cmdl.pos_args(); // [0] is the subcommand name
  if (cmdl[{"-h", "--help"}] || files.size() < 2)
  {
    fmt::print(stderr, "Usage: dew info <file.dew|dir://...|s3://...> [more datasets ...]\n");
    return cmdl[{"-h", "--help"}] ? 0 : 1;
  }
  if (!tool::check_options(cmdl, {}, {}))
    return 1;

  int exit_code = 0;
  for (size_t arg = 1; arg < files.size(); arg++)
  {
    const char *filename = files[arg].c_str();
    auto len = files[arg].size();

    if (files.size() > 2)
      fmt::print("=== {} ===\n", filename);

    // State sections come first: they read the raw superblock/manifest directly, so they also work
    // for datasets the full open below would refuse (e.g. an incomplete DEW2 bucket).
    {
      auto parsed = dew::converter::parse_url(filename);
      if (parsed.scheme.empty() || parsed.scheme == "file")
        print_local_cache_state(parsed.path);
      else if (parsed.scheme == "dir")
        print_dir_bucket_state(parsed.path);
    }

    dew_error_t *err = nullptr;
    converter_handle_t conv(dew_converter_create(filename, len, dew_open_file_semantics_read_only, &err));
    if (!conv)
    {
      const char *err_str = "unknown";
      size_t err_len = 0;
      if (err)
        dew_error_get_info(err, nullptr, &err_str, &err_len);
      fmt::print(stderr, "Error: failed to read '{}': {}\n", filename, err_str);
      if (err)
        dew_error_destroy(err);
      exit_code = 1;
      continue;
    }

    dew_converter_stats_t stats;
    dew_converter_get_compression_stats(conv, &stats);

    if (stats.attribute_count == 0 && stats.total_buffer_count == 0)
    {
      fmt::print("No compression statistics in this file.\n");
      if (arg + 1 < files.size())
        fmt::print("\n");
      continue;
    }

    bool has_lod = stats.lod_buffer_count > 0;
    uint32_t source_buffer_count = stats.total_buffer_count - stats.lod_buffer_count;

    fmt::print("Input files:   {}\n", format_number(stats.input_file_count));
    if (stats.input_file_size_bytes > 0)
      fmt::print("Source size:   {}\n", format_bytes(stats.input_file_size_bytes));
    if (has_lod)
      fmt::print("Total buffers: {} ({} source, {} LOD)\n",
                 format_number(stats.total_buffer_count),
                 format_number(source_buffer_count),
                 format_number(stats.lod_buffer_count));
    else
      fmt::print("Total buffers: {}\n", format_number(stats.total_buffer_count));
    fmt::print("Compression:   {}\n\n", method_name(stats.compression_method));

    if (has_lod)
    {
      // Source data table
      print_attribute_table("Source data", stats, [](const dew_converter_attribute_stats_t &a) -> table_row_t {
        return {a.buffer_count - a.lod_buffer_count,
                a.uncompressed_bytes - a.lod_uncompressed_bytes,
                a.compressed_bytes - a.lod_compressed_bytes};
      });

      fmt::print("\n");

      // LOD data table
      print_attribute_table("LOD data", stats, [](const dew_converter_attribute_stats_t &a) -> table_row_t {
        return {a.lod_buffer_count, a.lod_uncompressed_bytes, a.lod_compressed_bytes};
      });

      fmt::print("\n");

      // Combined total
      print_attribute_table("Combined", stats, [](const dew_converter_attribute_stats_t &a) -> table_row_t {
        return {a.buffer_count, a.uncompressed_bytes, a.compressed_bytes};
      });
    }
    else
    {
      // No LOD data — show combined table with range column (backward compat)
      fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>7s}  {}\n",
                 "Attribute", "Format", "Buffers", "Uncompressed", "Compressed", "Ratio", "Range");
      fmt::print("{:-<100s}\n", "");

      uint64_t total_uncompressed = 0;
      uint64_t total_compressed = 0;
      uint64_t total_buffers = 0;

      for (uint32_t i = 0; i < stats.attribute_count; i++)
      {
        auto &a = stats.attributes[i];
        double ratio = a.compressed_bytes > 0
          ? double(a.uncompressed_bytes) / double(a.compressed_bytes)
          : 0.0;
        std::string range_str;
        if (a.min_value <= a.max_value)
          range_str = fmt::format("[{:.6g}, {:.6g}]", a.min_value, a.max_value);
        fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>6.2f}x  {}\n",
                   a.name,
                   fmt::format("{}x{}", type_name(a.type), int(a.components)),
                   format_number(a.buffer_count),
                   format_bytes(a.uncompressed_bytes),
                   format_bytes(a.compressed_bytes),
                   ratio,
                   range_str);
        total_uncompressed += a.uncompressed_bytes;
        total_compressed += a.compressed_bytes;
        total_buffers += a.buffer_count;
      }

      fmt::print("{:-<100s}\n", "");
      double total_ratio = total_compressed > 0
        ? double(total_uncompressed) / double(total_compressed)
        : 0.0;
      fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>6.2f}x\n",
                 "Total", "",
                 format_number(total_buffers),
                 format_bytes(total_uncompressed),
                 format_bytes(total_compressed),
                 total_ratio);
    }

    if (stats.input_file_size_bytes > 0)
    {
      uint64_t total_compressed = 0;
      for (uint32_t i = 0; i < stats.attribute_count; i++)
        total_compressed += stats.attributes[i].compressed_bytes;
      if (total_compressed > 0)
      {
        double source_ratio = double(stats.input_file_size_bytes) / double(total_compressed);
        fmt::print("\nSource vs DEW:  {} -> {} ({:.2f}x)\n",
                   format_bytes(stats.input_file_size_bytes),
                   format_bytes(total_compressed),
                   source_ratio);
      }
    }

    // Print path selection stats for attributes that have non-trivial path counts
    bool has_path_stats = false;
    for (uint32_t i = 0; i < stats.attribute_count; i++)
    {
      auto &a = stats.attributes[i];
      uint64_t total_paths = a.path_counts[0] + a.path_counts[1] + a.path_counts[2] + a.path_counts[3];
      if (total_paths > 0 && (a.path_counts[1] > 0 || a.path_counts[2] > 0 || a.path_counts[3] > 0))
      {
        if (!has_path_stats)
        {
          fmt::print("\nCompression path selection:\n");
          has_path_stats = true;
        }
        fmt::print("  {} ({}x{}):", a.name, type_name(a.type), int(a.components));
        static const char *path_names[] = {"raw", "decorr", "delta", "decorr+delta"};
        for (int p = 0; p < 4; p++)
        {
          if (a.path_counts[p] > 0)
            fmt::print("  {}={}", path_names[p], format_number(a.path_counts[p]));
        }
        fmt::print("\n");
      }
    }

    // Performance stats
    dew_converter_perf_stats_t perf;
    dew_converter_get_perf_stats(conv, &perf);
    if (perf.total_time_seconds > 0)
    {
      fmt::print("\nPerformance stats:\n");
      fmt::print("  Total time:          {:.2f}s\n", perf.total_time_seconds);
      fmt::print("  Total written:       {:.2f} MB ({:.2f} MB/s)\n", perf.total_bytes_written_mb, perf.overall_mbps);
      if (perf.source_read.operation_count > 0)
      {
        double read_mb = double(perf.source_read.total_bytes) / 1e6;
        double read_s = double(perf.source_read.total_time_us) / 1e6;
        fmt::print("  Source reading:      {:.2f} MB in {:.2f}s ({:.2f} MB/s)\n", read_mb, read_s, perf.source_read.avg_mbps);
      }
      if (perf.sort.operation_count > 0)
        fmt::print("  Sorting:             avg {:.2f} MB/s, peak {:.2f} MB/s, low {:.2f} MB/s\n", perf.sort.avg_mbps, perf.sort.peak_mbps, perf.sort.low_mbps);
      if (perf.source_write.operation_count > 0)
        fmt::print("  Source write IO:     avg {:.2f} MB/s, peak {:.2f} MB/s, low {:.2f} MB/s\n", perf.source_write.avg_mbps, perf.source_write.peak_mbps, perf.source_write.low_mbps);
      if (perf.tree_build_seconds > 0)
        fmt::print("  Tree building:       {:.2f}s\n", perf.tree_build_seconds);
      if (perf.lod_generation_seconds > 0)
      {
        fmt::print("  LOD generation:      {:.2f}s\n", perf.lod_generation_seconds);
        if (perf.lod_read.operation_count > 0)
          fmt::print("    LOD read IO:       avg {:.2f} MB/s, peak {:.2f} MB/s, low {:.2f} MB/s\n", perf.lod_read.avg_mbps, perf.lod_read.peak_mbps, perf.lod_read.low_mbps);
        if (perf.lod_write.operation_count > 0)
          fmt::print("    LOD write IO:      avg {:.2f} MB/s, peak {:.2f} MB/s, low {:.2f} MB/s\n", perf.lod_write.avg_mbps, perf.lod_write.peak_mbps, perf.lod_write.low_mbps);
        if (perf.cache_hits > 0 || perf.cache_misses > 0)
        {
          uint64_t total = perf.cache_hits + perf.cache_misses;
          double hit_rate = total > 0 ? 100.0 * double(perf.cache_hits) / double(total) : 0.0;
          fmt::print("    Read cache:        {} hits, {} misses ({:.1f}% hit rate)\n", perf.cache_hits, perf.cache_misses, hit_rate);
        }
      }
    }

    if (arg + 1 < files.size())
      fmt::print("\n");
  }

  return exit_code;
}
