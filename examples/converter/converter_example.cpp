#include <fmt/printf.h>
#include <fmt/format.h>
#include <string>
#include <vector>
#include <cstring>
#include <cinttypes>
#include <numeric>

#include <points/converter/converter.h>

void converter_progress_callback_t(void *user_data, float progress)
{
  (void)user_data;
  fmt::print("Progress: {:.2f}\n", progress);
}

void converter_warning_callback_t(void *user_data, const char *message)
{
  (void)user_data;
  fmt::print("Warning: {}\n", message);
}

static std::string get_error_string(const points::error_t *error)
{
  int code;
  const char *str;
  size_t str_len;
  points::error_get_info(error, &code, &str, &str_len);
  return {str, str_len};
}

void converter_error_callback_t(void *user_data, const struct points::error_t *error)
{
  (void)user_data;
  auto error_str = get_error_string(error);
  fmt::print("Error: {}\n", error_str);
}

void converter_done_callback_t(void *user_data)
{
  (void)user_data;
  fmt::print("Done\n");
}

template <typename T, typename Deleter>
std::unique_ptr<T, Deleter> create_unique_ptr(T *t, Deleter d)
{
  return std::unique_ptr<T, Deleter>(t, d);
}

static points::converter::converter_compression_t parse_compression(const char *str)
{
  if (std::strcmp(str, "none") == 0)
    return points::converter::converter_compression_none;
  if (std::strcmp(str, "blosc2") == 0)
    return points::converter::converter_compression_blosc2;
  if (std::strcmp(str, "zstd") == 0)
    return points::converter::converter_compression_zstd;
  if (std::strcmp(str, "huff0") == 0)
    return points::converter::converter_compression_huff0;
  fmt::print(stderr, "Unknown compression '{}', using blosc2\n", str);
  return points::converter::converter_compression_blosc2;
}

static const char *type_name(points::type_t type)
{
  switch (type)
  {
  case points::type_u8: return "u8";
  case points::type_i8: return "i8";
  case points::type_u16: return "u16";
  case points::type_i16: return "i16";
  case points::type_u32: return "u32";
  case points::type_i32: return "i32";
  case points::type_m32: return "m32";
  case points::type_r32: return "r32";
  case points::type_u64: return "u64";
  case points::type_i64: return "i64";
  case points::type_m64: return "m64";
  case points::type_r64: return "r64";
  case points::type_m128: return "m128";
  case points::type_m192: return "m192";
  default: return "?";
  }
}

static std::string format_str(points::type_t type, points::components_t components)
{
  return fmt::format("{}x{}", type_name(type), static_cast<int>(components));
}

static const char *method_name(uint32_t method)
{
  switch (method)
  {
  case 0: return "none";
  case 1: return "blosc2";
  case 2: return "zstd";
  case 3: return "huff0";
  default: return "unknown";
  }
}

static std::string format_number(uint64_t n)
{
  auto s = std::to_string(n);
  std::string result;
  int count = 0;
  for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i)
  {
    if (count > 0 && count % 3 == 0)
      result.insert(result.begin(), ',');
    result.insert(result.begin(), s[static_cast<size_t>(i)]);
    count++;
  }
  return result;
}

static void print_compression_stats(const points::converter::converter_stats_t &stats)
{
  fmt::print("\nCompression Statistics:\n");
  fmt::print("  Input files:    {}\n", format_number(stats.input_file_count));
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

struct args_t
{
  std::vector<std::string> input;
  std::string output;
  points::converter::converter_compression_t compression;
  bool inspect = false;
};

args_t parse_arguments(int argc, char *argv[])
{
  args_t args = {{}, "out.jlp", points::converter::converter_compression_blosc2, false};
  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--out") == 0)
    {
      if (i + 1 < argc)
      {
        args.output = argv[++i];
      }
    }
    else if (std::strcmp(argv[i], "-c") == 0 || std::strcmp(argv[i], "--compression") == 0)
    {
      if (i + 1 < argc)
      {
        args.compression = parse_compression(argv[++i]);
      }
    }
    else if (std::strcmp(argv[i], "-i") == 0 || std::strcmp(argv[i], "--inspect") == 0)
    {
      args.inspect = true;
    }
    else
    {
      args.input.emplace_back(argv[i]);
    }
  }

  return args;
}

int main(int argc, char **argv)
{
  args_t args = parse_arguments(argc, argv);

  if (args.inspect)
  {
    if (args.input.empty())
    {
      fmt::print(stderr, "No file specified for --inspect\n");
      return 1;
    }
    auto &filename = args.input[0];
    points::converter::converter_stats_t stats;
    if (points::converter::converter_read_file_stats(filename.c_str(), filename.size(), &stats) != 0)
    {
      fmt::print(stderr, "Failed to read stats from '{}'\n", filename);
      return 1;
    }
    print_compression_stats(stats);
    return 0;
  }

  if (args.input.empty())
  {
    fmt::print("No input files specified\n");
    return 1;
  }

  std::vector<points::converter::str_buffer> input_str_buf(args.input.size());
  std::transform(args.input.begin(), args.input.end(), input_str_buf.begin(), [](const std::string &str) -> points::converter::str_buffer { return {str.c_str(), static_cast<uint32_t>(str.size())}; });

  points::error_t *create_error = nullptr;
  auto converter = create_unique_ptr(points::converter::converter_create(args.output.data(), args.output.size(), points::converter::open_file_semantics_truncate, &create_error), &points::converter::converter_destroy);
  if (!converter)
  {
    if (create_error)
    {
      auto error_str = get_error_string(create_error);
      fmt::print(stderr, "Failed to create converter: {}\n", error_str);
      points::error_destroy(create_error);
    }
    return 1;
  }
  points::converter::converter_runtime_callbacks_t runtime_callbacks = {&converter_progress_callback_t, &converter_warning_callback_t, &converter_error_callback_t, &converter_done_callback_t};
  points::converter::converter_set_runtime_callbacks(converter.get(), runtime_callbacks, nullptr);
  points::converter::converter_set_compression(converter.get(), args.compression);
  points::converter::converter_add_data_file(converter.get(), input_str_buf.data(), int(input_str_buf.size()));
  points::converter::converter_wait_idle(converter.get());

  points::converter::converter_stats_t stats;
  points::converter::converter_get_compression_stats(converter.get(), &stats);
  print_compression_stats(stats);

  return 0;
}
