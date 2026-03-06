/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#include "converter.hpp"
#include <points/converter/converter.h>

#include "compressor.hpp"
#include "perf_stats.hpp"
#include "processor.hpp"

#include <cstdio>
#include <memory>
#include <vector>


namespace points::converter
{
struct converter_t *converter_create(const char *url, uint64_t url_size, enum converter_open_file_semantics_t semantics, error_t **error)
{
  auto *converter = new converter_t(url, url_size, semantics);
  if (converter->error.code != 0)
  {
    if (error)
    {
      *error = new error_t();
      (*error)->code = converter->error.code;
      (*error)->msg = converter->error.msg;
    }
    delete converter;
    return nullptr;
  }
  return converter;
}

void converter_destroy(converter_t *destroy)
{
  delete destroy;
}

void converter_set_file_converter_callbacks(converter_t *converter, converter_file_convert_callbacks_t callbacks)
{
  converter->processor.set_converter_callbacks(callbacks);
}

void converter_set_runtime_callbacks(converter_t *converter, converter_runtime_callbacks_t callbacks, void *user_ptr)
{
  converter->processor.set_runtime_callbacks(callbacks, user_ptr);
}

void converter_set_compression(converter_t *converter, enum converter_compression_t compression)
{
  converter->processor.storage_handler().set_compressor(static_cast<compression_method_t>(compression));
}

void converter_add_data_file(converter_t *converter, str_buffer *buffers, uint32_t buffer_count)
{
  std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> input_files;
  input_files.reserve(buffer_count);
  for (uint32_t i = 0; i < buffer_count; i++)
  {
    input_files.emplace_back();
    auto &input_data_source = input_files.back();
    input_data_source.first.reset(new char[buffers[i].size + 1]);
    memcpy(input_data_source.first.get(), buffers[i].data, buffers[i].size);
    input_data_source.first.get()[buffers[i].size] = 0;
    input_data_source.second = buffers[i].size;
  }
  converter->processor.add_files(std::move(input_files));
}

void converter_wait_idle(converter_t *converter)
{
  converter->processor.wait_idle();
}

converter_conversion_status_t converter_status(converter_t *converter)
{
  if (converter->processor.has_errors())
    return conversion_status_error;
  return converter->status;
}

static void fill_converter_stats(const compression_stats_t &src, converter_stats_t *dst)
{
  memset(dst, 0, sizeof(*dst));
  dst->input_file_count = src.input_file_count;
  dst->total_buffer_count = src.total_buffer_count;
  dst->lod_buffer_count = src.lod_buffer_count;
  dst->compression_method = static_cast<uint32_t>(src.method);
  dst->attribute_count = static_cast<uint32_t>(std::min(src.per_attribute.size(), size_t(32)));
  for (uint32_t i = 0; i < dst->attribute_count; i++)
  {
    auto &s = src.per_attribute[i];
    auto &d = dst->attributes[i];
    auto name_len = std::min(s.name.size(), size_t(63));
    memcpy(d.name, s.name.data(), name_len);
    d.name[name_len] = '\0';
    d.type = s.format.type;
    d.components = s.format.components;
    d.buffer_count = s.buffer_count;
    d.uncompressed_bytes = s.uncompressed_bytes;
    d.compressed_bytes = s.compressed_bytes;
    d.min_value = s.min_value;    d.max_value = s.max_value;
    memcpy(d.path_counts, s.path_counts, sizeof(d.path_counts));
    d.lod_buffer_count = s.lod_buffer_count;
    d.lod_uncompressed_bytes = s.lod_uncompressed_bytes;
    d.lod_compressed_bytes = s.lod_compressed_bytes;
  }
}

void converter_get_compression_stats(struct converter_t *converter, struct converter_stats_t *stats)
{
  auto &src = converter->processor.storage_handler().get_compression_stats();
  fill_converter_stats(src, stats);
}

int converter_read_file_stats(const char *filename, uint64_t filename_size, struct converter_stats_t *stats)
{
  std::string path(filename, filename_size);
  FILE *f = nullptr;
#ifdef _MSC_VER
  fopen_s(&f, path.c_str(), "rb");
#else
  f = fopen(path.c_str(), "rb");
#endif
  if (!f)
    return -1;

  uint8_t index_buf[128];
  memset(index_buf, 0, sizeof(index_buf));
  if (fread(index_buf, 1, 128, f) != 128)
  {
    fclose(f);
    return -1;
  }

  // Check magic
  if (index_buf[0] != 'J' || index_buf[1] != 'L' || index_buf[2] != 'P' || index_buf[3] != 0)
  {
    fclose(f);
    return -1;
  }

  // Parse stats location: 4th storage_location_t at offset 4 + 3*16 = 52
  storage_location_t stats_loc;
  memcpy(&stats_loc, index_buf + 52, sizeof(stats_loc));

  if (stats_loc.size == 0 || stats_loc.offset == 0)
  {
    // No stats in this file
    fclose(f);
    memset(stats, 0, sizeof(*stats));
    return 0;
  }

  auto blob = std::make_unique<uint8_t[]>(stats_loc.size);
  if (fseek(f, static_cast<long>(stats_loc.offset), SEEK_SET) != 0)
  {
    fclose(f);
    return -1;
  }
  if (fread(blob.get(), 1, stats_loc.size, f) != stats_loc.size)
  {
    fclose(f);
    return -1;
  }
  fclose(f);

  auto parsed = compression_stats_t::deserialize(blob.get(), stats_loc.size);
  fill_converter_stats(parsed, stats);
  return 0;
}

static void fill_live_io_stats(converter_io_stats_t &dst, const io_counter_t &src)
{
  dst.total_bytes = src.total_bytes.load(std::memory_order_relaxed);
  dst.total_time_us = src.total_time_us.load(std::memory_order_relaxed);
  dst.operation_count = src.operation_count.load(std::memory_order_relaxed);
  dst.avg_mbps = src.avg_mbps();
  dst.peak_mbps = src.peak_mbps();
  dst.low_mbps = src.low_mbps();
}

static void fill_io_stats(converter_io_stats_t &dst, const perf_stats_t::deserialized_perf_stats_t::counter_data_t &src)
{
  dst.total_bytes = src.total_bytes;
  dst.total_time_us = src.total_time_us;
  dst.operation_count = src.operation_count;
  dst.avg_mbps = src.avg_mbps();
  dst.peak_mbps = src.peak_mbps();
  dst.low_mbps = src.low_mbps();
}

int converter_read_file_perf_stats(const char *filename, uint64_t filename_size, struct converter_perf_stats_t *perf_stats)
{
  std::string path(filename, filename_size);
  FILE *f = nullptr;
#ifdef _MSC_VER
  fopen_s(&f, path.c_str(), "rb");
#else
  f = fopen(path.c_str(), "rb");
#endif
  if (!f)
    return -1;

  uint8_t index_buf[128];
  memset(index_buf, 0, sizeof(index_buf));
  if (fread(index_buf, 1, 128, f) != 128)
  {
    fclose(f);
    return -1;
  }

  if (index_buf[0] != 'J' || index_buf[1] != 'L' || index_buf[2] != 'P' || index_buf[3] != 0)
  {
    fclose(f);
    return -1;
  }

  // perf_stats location is the 5th storage_location_t at offset 4 + 4*16 = 68
  storage_location_t perf_loc;
  memcpy(&perf_loc, index_buf + 68, sizeof(perf_loc));

  if (perf_loc.size == 0 || perf_loc.offset == 0)
  {
    fclose(f);
    memset(perf_stats, 0, sizeof(*perf_stats));
    return 0;
  }

  auto blob = std::make_unique<uint8_t[]>(perf_loc.size);
  if (fseek(f, static_cast<long>(perf_loc.offset), SEEK_SET) != 0)
  {
    fclose(f);
    return -1;
  }
  if (fread(blob.get(), 1, perf_loc.size, f) != perf_loc.size)
  {
    fclose(f);
    return -1;
  }
  fclose(f);

  auto parsed = perf_stats_t::deserialize(blob.get(), perf_loc.size);
  if (!parsed.valid)
  {
    memset(perf_stats, 0, sizeof(*perf_stats));
    return 0;
  }

  perf_stats->total_time_seconds = parsed.total_time_seconds;
  uint64_t written = parsed.source_write.total_bytes + parsed.lod_write.total_bytes;
  perf_stats->total_bytes_written_mb = double(written) / 1e6;
  perf_stats->overall_mbps = parsed.total_time_seconds > 0 ? perf_stats->total_bytes_written_mb / parsed.total_time_seconds : 0;
  fill_io_stats(perf_stats->source_read, parsed.source_read);
  fill_io_stats(perf_stats->sort, parsed.sort);
  fill_io_stats(perf_stats->source_write, parsed.source_write);
  fill_io_stats(perf_stats->lod_read, parsed.lod_read);
  fill_io_stats(perf_stats->lod_write, parsed.lod_write);
  perf_stats->tree_build_seconds = double(parsed.tree_build_us) / 1e6;
  perf_stats->lod_generation_seconds = double(parsed.lod_generation_us) / 1e6;
  perf_stats->cache_hits = parsed.cache_hits;
  perf_stats->cache_misses = parsed.cache_misses;
  return 0;
}

void converter_get_live_perf_stats(struct converter_t *converter, struct converter_perf_stats_t *perf_stats)
{
  auto &ps = converter->processor.perf_stats();
  auto now = perf_stats_t::clock_t::now();
  double elapsed = double(std::chrono::duration_cast<std::chrono::microseconds>(now - ps.conversion_start).count()) / 1e6;

  perf_stats->total_time_seconds = elapsed;
  uint64_t written = ps.source_write.total_bytes.load(std::memory_order_relaxed) + ps.lod_write.total_bytes.load(std::memory_order_relaxed);
  perf_stats->total_bytes_written_mb = double(written) / 1e6;
  perf_stats->overall_mbps = elapsed > 0 ? perf_stats->total_bytes_written_mb / elapsed : 0;

  fill_live_io_stats(perf_stats->source_read, ps.source_read);
  fill_live_io_stats(perf_stats->sort, ps.sort);
  fill_live_io_stats(perf_stats->source_write, ps.source_write);
  fill_live_io_stats(perf_stats->lod_read, ps.lod_read);
  fill_live_io_stats(perf_stats->lod_write, ps.lod_write);

  perf_stats->tree_build_seconds = double(ps.tree_build_time_us.load(std::memory_order_relaxed)) / 1e6;
  perf_stats->lod_generation_seconds = double(ps.lod_generation_time_us.load(std::memory_order_relaxed)) / 1e6;
  perf_stats->cache_hits = ps.cache_hits.load(std::memory_order_relaxed);
  perf_stats->cache_misses = ps.cache_misses.load(std::memory_order_relaxed);
}

} // namespace points::converter

