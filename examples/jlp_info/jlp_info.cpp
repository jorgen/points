#include <fmt/format.h>
#include <points/converter/converter.h>

#include <cstring>
#include <string>

using namespace points;
using namespace points::converter;

struct converter_handle_t
{
  converter_t *ptr = nullptr;
  converter_handle_t(converter_t *p) : ptr(p) {}
  ~converter_handle_t() { if (ptr) converter_destroy(ptr); }
  converter_handle_t(const converter_handle_t &) = delete;
  converter_handle_t &operator=(const converter_handle_t &) = delete;
  operator converter_t *() const { return ptr; }
  explicit operator bool() const { return ptr != nullptr; }
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

static const char *method_name(uint32_t method)
{
  switch (method)
  {
  case 0:  return "none";
  case 1:  return "blosc2";
  case 2:  return "zstd";
  case 3:  return "huff0";
  default: return "unknown";
  }
}

static std::string format_bytes(uint64_t bytes)
{
  if (bytes < 1024)
    return fmt::format("{} B", bytes);
  if (bytes < 1024 * 1024)
    return fmt::format("{:.1f} KB", bytes / 1024.0);
  if (bytes < 1024 * 1024 * 1024)
    return fmt::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
  return fmt::format("{:.2f} GB", bytes / (1024.0 * 1024.0 * 1024.0));
}

static std::string format_number(uint64_t n)
{
  auto s = std::to_string(n);
  std::string result;
  int count = 0;
  for (int i = int(s.size()) - 1; i >= 0; --i)
  {
    if (count > 0 && count % 3 == 0)
      result.insert(result.begin(), ',');
    result.insert(result.begin(), s[size_t(i)]);
    count++;
  }
  return result;
}

struct table_row_t
{
  uint64_t buffer_count;
  uint64_t uncompressed_bytes;
  uint64_t compressed_bytes;
};

static void print_attribute_table(const char *title, const converter_stats_t &stats,
                                  table_row_t (*get_row)(const converter_attribute_stats_t &))
{
  fmt::print("{}:\n", title);
  fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>7s}\n",
             "Attribute", "Format", "Buffers", "Uncompressed", "Compressed", "Ratio");
  fmt::print("{:-<80s}\n", "");

  uint64_t total_uncompressed = 0;
  uint64_t total_compressed = 0;
  uint64_t total_buffers = 0;

  for (uint32_t i = 0; i < stats.attribute_count; i++)
  {
    auto &a = stats.attributes[i];
    auto row = get_row(a);
    if (row.buffer_count == 0)
      continue;
    double ratio = row.compressed_bytes > 0
      ? double(row.uncompressed_bytes) / double(row.compressed_bytes)
      : 0.0;
    fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>6.2f}x\n",
               a.name,
               fmt::format("{}x{}", type_name(a.type), int(a.components)),
               format_number(row.buffer_count),
               format_bytes(row.uncompressed_bytes),
               format_bytes(row.compressed_bytes),
               ratio);
    total_uncompressed += row.uncompressed_bytes;
    total_compressed += row.compressed_bytes;
    total_buffers += row.buffer_count;
  }

  fmt::print("{:-<80s}\n", "");
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

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fmt::print(stderr, "Usage: jlp_info <file.jlp> [file2.jlp ...]\n");
    return 1;
  }

  int exit_code = 0;
  for (int arg = 1; arg < argc; arg++)
  {
    const char *filename = argv[arg];
    auto len = strlen(filename);

    if (argc > 2)
      fmt::print("=== {} ===\n", filename);

    error_t *err = nullptr;
    converter_handle_t conv(converter_create(filename, len, open_file_semantics_read_only, &err));
    if (!conv)
    {
      const char *err_str = "unknown";
      size_t err_len = 0;
      if (err)
        error_get_info(err, nullptr, &err_str, &err_len);
      fmt::print(stderr, "Error: failed to read '{}': {}\n", filename, err_str);
      if (err)
        error_destroy(err);
      exit_code = 1;
      continue;
    }

    converter_stats_t stats;
    converter_get_compression_stats(conv, &stats);

    if (stats.attribute_count == 0 && stats.total_buffer_count == 0)
    {
      fmt::print("No compression statistics in this file.\n");
      if (arg + 1 < argc)
        fmt::print("\n");
      continue;
    }

    bool has_lod = stats.lod_buffer_count > 0;
    uint32_t source_buffer_count = stats.total_buffer_count - stats.lod_buffer_count;

    fmt::print("Input files:   {}\n", format_number(stats.input_file_count));
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
      print_attribute_table("Source data", stats, [](const converter_attribute_stats_t &a) -> table_row_t {
        return {a.buffer_count - a.lod_buffer_count,
                a.uncompressed_bytes - a.lod_uncompressed_bytes,
                a.compressed_bytes - a.lod_compressed_bytes};
      });

      fmt::print("\n");

      // LOD data table
      print_attribute_table("LOD data", stats, [](const converter_attribute_stats_t &a) -> table_row_t {
        return {a.lod_buffer_count, a.lod_uncompressed_bytes, a.lod_compressed_bytes};
      });

      fmt::print("\n");

      // Combined total
      print_attribute_table("Combined", stats, [](const converter_attribute_stats_t &a) -> table_row_t {
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

    if (arg + 1 < argc)
      fmt::print("\n");
  }

  return exit_code;
}
