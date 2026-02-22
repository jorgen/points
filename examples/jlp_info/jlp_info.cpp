#include <fmt/format.h>
#include <points/converter/converter.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace points;
using namespace points::converter;

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

    converter_stats_t stats;
    if (converter_read_file_stats(filename, len, &stats) != 0)
    {
      fmt::print(stderr, "Error: failed to read '{}'\n", filename);
      exit_code = 1;
      continue;
    }

    if (stats.attribute_count == 0 && stats.total_buffer_count == 0)
    {
      fmt::print("No compression statistics in this file.\n");
      if (arg + 1 < argc)
        fmt::print("\n");
      continue;
    }

    fmt::print("Input files:   {}\n", format_number(stats.input_file_count));
    fmt::print("Total buffers: {}\n", format_number(stats.total_buffer_count));
    fmt::print("Compression:   {}\n\n", method_name(stats.compression_method));

    fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>7s}\n",
               "Attribute", "Format", "Buffers", "Uncompressed", "Compressed", "Ratio");
    fmt::print("{:-<75s}\n", "");

    uint64_t total_uncompressed = 0;
    uint64_t total_compressed = 0;
    uint64_t total_buffers = 0;

    for (uint32_t i = 0; i < stats.attribute_count; i++)
    {
      auto &a = stats.attributes[i];
      double ratio = a.compressed_bytes > 0
        ? double(a.uncompressed_bytes) / double(a.compressed_bytes)
        : 0.0;
      fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>6.2f}x\n",
                 a.name,
                 fmt::format("{}x{}", type_name(a.type), int(a.components)),
                 format_number(a.buffer_count),
                 format_bytes(a.uncompressed_bytes),
                 format_bytes(a.compressed_bytes),
                 ratio);
      total_uncompressed += a.uncompressed_bytes;
      total_compressed += a.compressed_bytes;
      total_buffers += a.buffer_count;
    }

    fmt::print("{:-<75s}\n", "");
    double total_ratio = total_compressed > 0
      ? double(total_uncompressed) / double(total_compressed)
      : 0.0;
    fmt::print("{:<20s} {:<10s} {:>8s} {:>14s} {:>14s} {:>6.2f}x\n",
               "Total", "",
               format_number(total_buffers),
               format_bytes(total_uncompressed),
               format_bytes(total_compressed),
               total_ratio);

    if (arg + 1 < argc)
      fmt::print("\n");
  }

  return exit_code;
}
