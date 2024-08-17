#include <fmt/printf.h>
#include <string>
#include <vector>

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

struct args_t
{
  std::vector<std::string> input;
  std::string output;
};

args_t parse_arguments(int argc, char *argv[])
{
  args_t args = {{}, "out.jlp"};
  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--out") == 0)
    {
      if (i + 1 < argc)
      {
        args.output = argv[++i];
      }
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
  if (args.input.empty())
  {
    fmt::print("No input files specified\n");
    return 1;
  }

  std::vector<points::converter::str_buffer> input_str_buf(args.input.size());
  std::transform(args.input.begin(), args.input.end(), input_str_buf.begin(), [](const std::string &str) -> points::converter::str_buffer { return {str.c_str(), static_cast<uint32_t>(str.size())}; });

  auto converter = create_unique_ptr(points::converter::converter_create(args.output.data(), args.output.size(), points::converter::open_file_semantics_truncate), &points::converter::converter_destroy);
  points::converter::converter_runtime_callbacks_t runtime_callbacks = {&converter_progress_callback_t, &converter_warning_callback_t, &converter_error_callback_t, &converter_done_callback_t};
  points::converter::converter_set_runtime_callbacks(converter.get(), runtime_callbacks, nullptr);
  points::converter::converter_add_data_file(converter.get(), input_str_buf.data(), int(input_str_buf.size()));
  points::converter::converter_wait_idle(converter.get());
  return 0;
}
