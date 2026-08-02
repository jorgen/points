#include "commands.hpp"
#include "tool_common.hpp"

#include <argh.h>
#include <fmt/printf.h>
#include <fmt/format.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <numeric>

#include <dew/converter/connection_cli.h>
#include <dew/converter/converter.h>

namespace
{

using tool::format_number;
using tool::get_error_string;
using tool::method_name;
using tool::type_name;

struct callback_data_t
{
  bool had_errors = false;
  dew_converter_t *converter = nullptr;
};

void converter_progress_callback_t(void *user_data, float progress)
{
  (void)progress;
  auto *data = static_cast<callback_data_t *>(user_data);
  dew_converter_perf_stats_t stats;
  dew_converter_get_live_perf_stats(data->converter, &stats);
  fmt::print(stderr, "\r[{:.1f}s] read: {} ops  sort: {} ops  write: {} ops  {:.1f} MB/s",
             stats.total_time_seconds,
             stats.source_read.operation_count,
             stats.sort.operation_count,
             stats.source_write.operation_count,
             stats.overall_mbps);
  // Destination mode only (returns false otherwise): the incremental-upload side of the pipeline.
  dew_converter_upload_state_t upload = {};
  if (dew_converter_get_upload_state(data->converter, &upload))
  {
    fmt::print(stderr, "  up: {:.1f} MB / {} bands{}", double(upload.bytes_uploaded) / (1024.0 * 1024.0), upload.bands_committed, upload.upload_parked ? " [PARKED]" : "");
    if (upload.cache_max_bytes)
      fmt::print(stderr, "  cache: {:.0f}/{:.0f} MB", double(upload.cache_resident_bytes) / (1024.0 * 1024.0), double(upload.cache_max_bytes) / (1024.0 * 1024.0));
  }
}

void converter_warning_callback_t(void *user_data, const char *message)
{
  (void)user_data;
  fmt::print("Warning: {}\n", message);
}

void converter_error_callback_t(void *user_data, const struct dew_error_t *error)
{
  auto *data = static_cast<callback_data_t *>(user_data);
  data->had_errors = true;
  auto error_str = get_error_string(error);
  fmt::print("Error: {}\n", error_str);
}

void converter_done_callback_t(void *user_data)
{
  (void)user_data;
  fmt::print(stderr, "\n");
}

void upload_error_callback_t(void *user_data, const struct dew_error_t *error, uint8_t parked)
{
  auto *data = static_cast<callback_data_t *>(user_data);
  if (parked)
    data->had_errors = true; // parked = upload gave up (resume by reopening later)
  auto error_str = get_error_string(error);
  fmt::print(stderr, "\nUpload error{}: {}\n", parked ? " (parked)" : "", error_str);
}

void upload_done_callback_t(void *user_data)
{
  (void)user_data;
  fmt::print(stderr, "\nUpload complete\n");
}

template <typename T, typename Deleter>
std::unique_ptr<T, Deleter> create_unique_ptr(T *t, Deleter d)
{
  return std::unique_ptr<T, Deleter>(t, d);
}

dew_converter_compression_t parse_compression(const char *str)
{
  if (std::strcmp(str, "none") == 0)
    return dew_converter_compression_none;
  if (std::strcmp(str, "zstd") == 0)
    return dew_converter_compression_zstd;
  if (std::strcmp(str, "huff0") == 0)
    return dew_converter_compression_huff0;
  fmt::print(stderr, "Unknown compression '{}', using zstd\n", str);
  return dew_converter_compression_zstd;
}

std::string format_str(dew_type_t type, dew_components_t components)
{
  return fmt::format("{}x{}", type_name(type), static_cast<int>(components));
}

void print_compression_stats(const dew_converter_stats_t &stats)
{
  fmt::print("\nCompression Statistics:\n");
  fmt::print("  Input files:    {}\n", format_number(stats.input_file_count));
  if (stats.input_file_size_bytes > 0)
  {
    double size_gb = double(stats.input_file_size_bytes) / (1024.0 * 1024.0 * 1024.0);
    fmt::print("  Source size:    {:.2f} GB\n", size_gb);
  }
  fmt::print("  Total buffers:  {}\n", format_number(stats.total_buffer_count));
  fmt::print("  Method:         {}\n", method_name(stats.compression_method));
  fmt::print("\n");

  fmt::print("  {:<20s} {:<8s} {:>10s} {:>16s} {:>16s} {:>8s}\n", "Attribute", "Format", "Buffers", "Uncompressed", "Compressed", "Ratio");

  uint64_t total_buffers = 0, total_uncompressed = 0, total_compressed = 0;
  for (uint32_t i = 0; i < stats.attribute_count; i++)
  {
    auto &a = stats.attributes[i];
    double ratio = a.compressed_bytes > 0 ? static_cast<double>(a.uncompressed_bytes) / static_cast<double>(a.compressed_bytes) : 0.0;
    fmt::print("  {:<20s} {:<8s} {:>10s} {:>16s} {:>16s} {:>7.2f}x\n",
               a.name, format_str(a.type, a.components),
               format_number(a.buffer_count), format_number(a.uncompressed_bytes),
               format_number(a.compressed_bytes), ratio);
    total_buffers += a.buffer_count;
    total_uncompressed += a.uncompressed_bytes;
    total_compressed += a.compressed_bytes;
  }

  fmt::print("  {:-<87s}\n", "");
  double total_ratio = total_compressed > 0 ? static_cast<double>(total_uncompressed) / static_cast<double>(total_compressed) : 0.0;
  fmt::print("  {:<20s} {:<8s} {:>10s} {:>16s} {:>16s} {:>7.2f}x\n",
             "Total", "", format_number(total_buffers), format_number(total_uncompressed),
             format_number(total_compressed), total_ratio);
}

void print_perf_stats(const dew_converter_perf_stats_t &ps)
{
  double overall = ps.total_time_seconds > 0 ? ps.total_bytes_written_mb / ps.total_time_seconds : 0;

  fmt::print(stderr, "\n--- Performance Summary ---\n");
  fmt::print(stderr, "  Total time:          {:.2f}s\n", ps.total_time_seconds);
  fmt::print(stderr, "  Total written:       {:.2f} MB ({:.2f} MB/s)\n", ps.total_bytes_written_mb, overall);

  if (ps.source_read.operation_count > 0)
  {
    double read_mb = double(ps.source_read.total_bytes) / 1e6;
    double read_s = double(ps.source_read.total_time_us) / 1e6;
    fmt::print(stderr, "  Source reading:      {:.2f} MB in {:.2f}s ({:.2f} MB/s)\n", read_mb, read_s, ps.source_read.avg_mbps);
  }

  if (ps.sort.operation_count > 0)
    fmt::print(stderr, "  Sorting:             avg {:.2f} MB/s, peak {:.2f} MB/s, low {:.2f} MB/s\n", ps.sort.avg_mbps, ps.sort.peak_mbps, ps.sort.low_mbps);

  if (ps.source_write.operation_count > 0)
    fmt::print(stderr, "  Source write IO:     avg {:.2f} MB/s, peak {:.2f} MB/s, low {:.2f} MB/s\n", ps.source_write.avg_mbps, ps.source_write.peak_mbps, ps.source_write.low_mbps);

  if (ps.tree_build_seconds > 0)
    fmt::print(stderr, "  Tree building:       {:.2f}s\n", ps.tree_build_seconds);

  if (ps.lod_generation_seconds > 0)
  {
    fmt::print(stderr, "  LOD generation:      {:.2f}s\n", ps.lod_generation_seconds);
    if (ps.lod_read.operation_count > 0)
      fmt::print(stderr, "    LOD read IO:       avg {:.2f} MB/s, peak {:.2f} MB/s, low {:.2f} MB/s\n", ps.lod_read.avg_mbps, ps.lod_read.peak_mbps, ps.lod_read.low_mbps);
    if (ps.lod_write.operation_count > 0)
      fmt::print(stderr, "    LOD write IO:      avg {:.2f} MB/s, peak {:.2f} MB/s, low {:.2f} MB/s\n", ps.lod_write.avg_mbps, ps.lod_write.peak_mbps, ps.lod_write.low_mbps);
  }
  fmt::print(stderr, "---\n");
}

struct args_t
{
  std::vector<std::string> input;
  std::string output;
  std::string connection;        // --connection spec (inline / @file / env:VAR) for a cloud output URL
  std::string cache;             // --cache: explicit local cache file for a cloud output (destination mode)
  uint64_t cache_max_bytes = 0;  // --cache-max-bytes: resident cap for the cache file; 0 = unlimited
  dew_converter_compression_t compression;
  bool inspect = false;
  uint32_t node_point_limit = 0; // points per node / blob-size lever; 0 = converter default
};

// Byte counts accept an optional K/M/G suffix (binary units).
uint64_t parse_byte_size(const char *str)
{
  char *end = nullptr;
  uint64_t value = std::strtoull(str, &end, 10);
  if (end && *end)
  {
    switch (*end)
    {
    case 'k': case 'K': value <<= 10; break;
    case 'm': case 'M': value <<= 20; break;
    case 'g': case 'G': value <<= 30; break;
    default: break;
    }
  }
  return value;
}

void print_convert_usage()
{
  fmt::print(stderr, "Usage: dew convert [options] <input.las|laz> [more inputs ...]\n\n");
  fmt::print(stderr, "Convert point cloud input into a .dew dataset -- a local packed file or, with a cloud\n");
  fmt::print(stderr, "URL, uploaded incrementally while the conversion runs.\n\n");
  fmt::print(stderr, "Options:\n");
  fmt::print(stderr, "  -o, --out <url>          output: file path, dir://, s3://, az:// (default: out.dew)\n");
  fmt::print(stderr, "  -C, --connection <spec>  connection string for a cloud output (inline / @file / env:VAR)\n");
  fmt::print(stderr, "  -c, --compression <m>    none | zstd | huff0 (default: zstd)\n");
  fmt::print(stderr, "  -n, --node-points <N>    points per octree node (the blob-size lever)\n");
  fmt::print(stderr, "      --cache <path>       explicit local cache file for a cloud output\n");
  fmt::print(stderr, "      --cache-max-bytes <N[K|M|G]>  resident cap for the cache file\n");
  fmt::print(stderr, "  -i, --inspect            print a dataset's stats instead of converting\n");
}

bool parse_arguments(int argc, char **argv, args_t &args, int &exit_code)
{
  argh::parser cmdl;
  cmdl.add_params({"-o", "--out", "-u", "--url", "-C", "--connection", "-c", "--compression", "-n", "--node-points", "--cache", "--cache-max-bytes"});
  cmdl.parse(argc, argv);

  if (cmdl[{"-h", "--help"}])
  {
    print_convert_usage();
    exit_code = 0; // help is not an error
    return false;
  }
  if (!tool::check_options(cmdl, {"i", "inspect"}, {"o", "out", "u", "url", "C", "connection", "c", "compression", "n", "node-points", "cache", "cache-max-bytes"}))
    return false;

  for (size_t i = 1; i < cmdl.pos_args().size(); i++)
    args.input.emplace_back(cmdl[i]);

  if (auto v = cmdl({"-o", "--out", "-u", "--url"}))
    args.output = v.str();
  args.connection = cmdl({"-C", "--connection"}).str();
  if (auto v = cmdl({"-c", "--compression"}))
    args.compression = parse_compression(v.str().c_str());
  if (auto v = cmdl({"-n", "--node-points"}))
  {
    if (!tool::parse_u32(v.str(), args.node_point_limit))
    {
      fmt::print(stderr, "Error: --node-points requires a non-negative integer\n");
      return false;
    }
  }
  args.cache = cmdl("--cache").str();
  if (auto v = cmdl("--cache-max-bytes"))
    args.cache_max_bytes = parse_byte_size(v.str().c_str());
  args.inspect = cmdl[{"-i", "--inspect"}];

  return true;
}

} // namespace

int cmd_convert(int argc, char **argv)
{
  args_t args;
  args.output = "out.dew";
  args.compression = dew_converter_compression_zstd;
  int parse_exit = 1;
  if (!parse_arguments(argc, argv, args, parse_exit))
    return parse_exit;

  if (args.inspect)
  {
    if (args.input.empty())
    {
      fmt::print(stderr, "No file specified for --inspect\n");
      return 1;
    }
    auto &filename = args.input[0];
    dew_error_t *err = nullptr;
    auto *conv = dew_converter_create(filename.c_str(), filename.size(), dew_open_file_semantics_read_only, &err);
    if (!conv)
    {
      const char *err_str = "unknown";
      size_t err_len = 0;
      if (err)
        dew_error_get_info(err, nullptr, &err_str, &err_len);
      fmt::print(stderr, "Failed to read stats from '{}': {}\n", filename, err_str);
      if (err)
        dew_error_destroy(err);
      return 1;
    }
    dew_converter_stats_t stats;
    dew_converter_get_compression_stats(conv, &stats);
    print_compression_stats(stats);
    dew_converter_destroy(conv);
    return 0;
  }

  if (args.input.empty())
  {
    print_convert_usage();
    return 1;
  }

  std::vector<dew_converter_str_buffer> input_str_buf(args.input.size());
  std::transform(args.input.begin(), args.input.end(), input_str_buf.begin(), [](const std::string &str) -> dew_converter_str_buffer { return {str.c_str(), static_cast<uint32_t>(str.size())}; });

  std::string connection;
  {
    std::string conn_error;
    if (!dew::converter::cli::resolve_connection_spec(args.connection, connection, conn_error))
    {
      fmt::print(stderr, "Connection error: {}\n", conn_error);
      return 1;
    }
  }
  dew_error_t *create_error = nullptr;
  // --cache pins an explicit local cache file for a cloud destination; without it a cloud URL still
  // converts through an implicit cache in the OS cache dir (create_with_connection reroutes).
  auto converter = args.cache.empty()
                     ? create_unique_ptr(dew_converter_create_with_connection(args.output.data(), args.output.size(), connection.data(), connection.size(), dew_open_file_semantics_truncate, &create_error), &dew_converter_destroy)
                     : create_unique_ptr(dew_converter_create_with_destination(args.cache.data(), args.cache.size(), args.output.data(), args.output.size(), connection.data(), connection.size(), dew_open_file_semantics_truncate, &create_error),
                                         &dew_converter_destroy);
  if (!converter)
  {
    if (create_error)
    {
      auto error_str = get_error_string(create_error);
      fmt::print(stderr, "Failed to create converter: {}\n", error_str);
      dew_error_destroy(create_error);
    }
    return 1;
  }
  if (args.cache_max_bytes)
    dew_converter_set_cache_max_bytes(converter.get(), args.cache_max_bytes);
  callback_data_t cb_data;
  cb_data.converter = converter.get();
  dew_converter_runtime_callbacks_t runtime_callbacks = {&converter_progress_callback_t, &converter_warning_callback_t, &converter_error_callback_t, &converter_done_callback_t};
  dew_converter_set_runtime_callbacks(converter.get(), runtime_callbacks, &cb_data);
  dew_converter_upload_callbacks_t upload_callbacks = {};
  upload_callbacks.error = &upload_error_callback_t;
  upload_callbacks.done = &upload_done_callback_t;
  dew_converter_set_upload_callbacks(converter.get(), upload_callbacks, &cb_data);
  dew_converter_set_compression(converter.get(), args.compression);
  if (args.node_point_limit > 0)
    dew_converter_set_node_point_limit(converter.get(), args.node_point_limit);
  dew_converter_add_data_file(converter.get(), input_str_buf.data(), int(input_str_buf.size()));
  dew_converter_wait_idle(converter.get());

  dew_converter_perf_stats_t perf_stats;
  dew_converter_get_live_perf_stats(converter.get(), &perf_stats);
  print_perf_stats(perf_stats);

  dew_converter_stats_t stats;
  dew_converter_get_compression_stats(converter.get(), &stats);
  print_compression_stats(stats);

  return cb_data.had_errors ? 1 : 0;
}
