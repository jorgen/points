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
#include <points/converter/converter.h>
#include <points/converter/default_attribute_names.h>
#include <points/converter/laszip_file_convert_callbacks.h>

#include "error.hpp"

#include <fmt/format.h>
#include <laszip/laszip_api.h>

#include <assert.h>

namespace points::converter
{

struct load_dll_t
{
  load_dll_t()
  {
#ifndef WIN32
    laszip_load_dll();
#endif
  }
};

load_dll_t load_dll;

struct laszip_handle_t
{
  std::string filename;
  laszip_POINTER reader = nullptr;
  laszip_point *point = nullptr;
  uint64_t point_count = 0;
  uint64_t point_read = 0;
  uint8_t las_format;

  ~laszip_handle_t()
  {
    if (reader)
    {
      if (laszip_close_reader(reader))
      {
        fprintf(stderr, "Failed to close laszip reader\n");
      }

      if (laszip_destroy(reader))
      {
        fprintf(stderr, "Failed to destroy laszip reader\n");
      }
    }
  }
};

template <size_t N>
void add_attribute(attributes_t *attributes, const char (&name)[N], type_t format, components_t components)
{
  attributes_add_attribute(attributes, name, N - 1, format, components);
}

static void add_attributes_format_0(attributes_t *attributes)
{
  add_attribute(attributes, POINTS_ATTRIBUTE_XYZ, type_i32, components_3);
  add_attribute(attributes, POINTS_ATTRIBUTE_INTENSITY, type_u16, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_LAS_COMPOSITE_0, type_u8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_CLASSIFICATION, type_u8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_SCAN_ANGLE_RANK, type_i8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_USER_DATA, type_u8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_POINT_SOURCE_ID, type_u16, components_1);
}

static void add_attributes_format_1(attributes_t *attributes)
{
  add_attributes_format_0(attributes);
  add_attribute(attributes, POINTS_ATTRIBUTE_GPS_TIME, type_r64, components_1);
}

static void add_attributes_format_2(attributes_t *attributes)
{
  add_attributes_format_0(attributes);
  add_attribute(attributes, POINTS_ATTRIBUTE_RGB, type_u16, components_3);
}

static void add_attributes_format_3(attributes_t *attributes)
{
  add_attributes_format_1(attributes);
  add_attribute(attributes, POINTS_ATTRIBUTE_RGB, type_u16, components_3);
}

static void add_wave_packets(attributes_t *attributes)
{
  add_attribute(attributes, POINTS_ATTRIBUTE_WAVE_PACKET_DESCRIPTOR_INDEX, type_u8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_BYTE_OFFSET_TO_WAVEFORM_DATA, type_r64, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_WAVEFORM_PACKET_SIZE_BYTES, type_u32, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_RETURN_POINT_WAVEFORM_LOCATION, type_r64, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_XYZ_T, type_r64, components_3);
}

static void add_attributes_format_4(attributes_t *attributes)
{
  add_attributes_format_1(attributes);
  add_wave_packets(attributes);
}

static void add_attributes_format_5(attributes_t *attributes)
{
  add_attributes_format_3(attributes);
  add_wave_packets(attributes);
}

static void add_attributes_format_6(attributes_t *attributes)
{
  add_attribute(attributes, POINTS_ATTRIBUTE_XYZ, type_i32, components_3);
  add_attribute(attributes, POINTS_ATTRIBUTE_INTENSITY, type_u16, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_LAS_COMPOSITE_1, type_u8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_LAS_COMPOSITE_2, type_u8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_CLASSIFICATION, type_u8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_USER_DATA, type_u8, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_SCAN_ANGLE, type_i16, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_POINT_SOURCE_ID, type_u16, components_1);
  add_attribute(attributes, POINTS_ATTRIBUTE_GPS_TIME, type_r64, components_1);
}

static void add_attributes_format_7(attributes_t *attributes)
{
  add_attributes_format_6(attributes);
  add_attribute(attributes, POINTS_ATTRIBUTE_RGB, type_u16, components_3);
}

static void add_attributes_format_8(attributes_t *attributes)
{
  add_attributes_format_7(attributes);
  add_attribute(attributes, POINTS_ATTRIBUTE_NEAR_INFRARED, type_u16, components_1);
}

static void add_attributes_format_9(attributes_t *attributes)
{
  add_attributes_format_6(attributes);
  add_wave_packets(attributes);
}

static void add_attributes_format_10(attributes_t *attributes)
{
  add_attributes_format_7(attributes);
  add_wave_packets(attributes);
}

static converter_file_pre_init_info_t laszip_converter_file_get_aabb_min(const char *filename, size_t filename_size, struct error_t **error)
{
  (void)filename;
  (void)filename_size;
  (void)error;
  converter_file_pre_init_info_t ret;
  ret.found_aabb_min = false;
  ret.found_point_count = false;
  std::unique_ptr<laszip_handle_t> laszip_handle(new laszip_handle_t());
  if (laszip_create(&laszip_handle->reader))
  {
    *error = new error_t();
    auto e = *error;
    e->code = -1;
    e->msg = "Failed to create laszip reader.";
    return ret;
  }

  laszip_BOOL is_compressed = 0;
  std::string filename_str(filename, filename_size);
  laszip_handle->filename = filename_str;
  if (laszip_open_reader(laszip_handle->reader, filename_str.c_str(), &is_compressed))
  {
    *error = new error_t();
    auto e = *error;
    e->code = -1;
    e->msg = fmt::format("Failed opening laszip reader for '{}'.", filename_str);
    return ret;
  }

  laszip_header_struct *lasheader;
  if (laszip_get_header_pointer(laszip_handle->reader, &lasheader))
  {
    *error = new error_t();
    auto e = *error;
    e->code = -1;
    e->msg = fmt::format("Failed to read laszip header for '{}'.", filename_str);
    return ret;
  }

  laszip_handle->point_count = (lasheader->number_of_point_records ? lasheader->number_of_point_records : lasheader->extended_number_of_point_records);

  ret.aabb_min[0] = lasheader->min_x;
  ret.aabb_min[1] = lasheader->min_y;
  ret.aabb_min[2] = lasheader->min_z;
  ret.approximate_point_count = laszip_handle->point_count;
  ret.approximate_point_size_bytes = (uint8_t)lasheader->point_data_record_length;
  ret.found_aabb_min = true;
  ret.found_point_count = true;
  return ret;
}

static void laszip_converter_file_init(const char *filename, size_t filename_size, header_t *header, attributes_t *attributes, void **user_ptr, struct error_t **error)
{
  std::unique_ptr<laszip_handle_t> laszip_handle(new laszip_handle_t());
  if (laszip_create(&laszip_handle->reader))
  {
    *error = new error_t();
    auto e = *error;
    e->code = -1;
    e->msg = "Failed to create laszip reader.";
    return;
  }

  laszip_BOOL is_compressed = 0;
  std::string filename_str(filename, filename_size);
  laszip_handle->filename = filename_str;
  if (laszip_open_reader(laszip_handle->reader, filename_str.c_str(), &is_compressed))
  {
    *error = new error_t();
    auto e = *error;
    e->code = -1;
    e->msg = fmt::format("Failed opening laszip reader for '{}'.", filename_str);
    return;
  }

  laszip_header_struct *lasheader;
  if (laszip_get_header_pointer(laszip_handle->reader, &lasheader))
  {
    *error = new error_t();
    auto e = *error;
    e->code = -1;
    e->msg = fmt::format("Failed to read laszip header for '{}'.", filename_str);
    return;
  }

  if (laszip_get_point_pointer(laszip_handle->reader, &laszip_handle->point))
  {
    *error = new error_t();
    auto e = *error;
    e->code = -1;
    e->msg = fmt::format("Failed to getting point pointer from laszip reader '{}'.", filename_str);
    return;
  }

  laszip_handle->point_count = (lasheader->number_of_point_records ? lasheader->number_of_point_records : lasheader->extended_number_of_point_records);
  header->point_count = laszip_handle->point_count;
  header->offset[0] = lasheader->x_offset;
  header->offset[1] = lasheader->y_offset;
  header->offset[2] = lasheader->z_offset;
  header->scale[0] = lasheader->x_scale_factor;
  header->scale[1] = lasheader->y_scale_factor;
  header->scale[2] = lasheader->z_scale_factor;

  header->min[0] = lasheader->min_x;
  header->min[1] = lasheader->min_y;
  header->min[2] = lasheader->min_z;
  header->max[0] = lasheader->max_x;
  header->max[1] = lasheader->max_y;
  header->max[2] = lasheader->max_z;

  laszip_handle->las_format = lasheader->point_data_format;
  switch (lasheader->point_data_format)
  {
  case 0:
    add_attributes_format_0(attributes);
    break;
  case 1:
    add_attributes_format_1(attributes);
    break;
  case 2:
    add_attributes_format_2(attributes);
    break;
  case 3:
    add_attributes_format_3(attributes);
    break;
  case 4:
    add_attributes_format_4(attributes);
    break;
  case 5:
    add_attributes_format_5(attributes);
    break;
  case 6:
    add_attributes_format_6(attributes);
    break;
  case 7:
    add_attributes_format_7(attributes);
    break;
  case 8:
    add_attributes_format_8(attributes);
    break;
  case 9:
    add_attributes_format_9(attributes);
    break;
  case 10:
    add_attributes_format_10(attributes);
    break;
  default:
    assert(false);
  }

  *user_ptr = laszip_handle.get();
  laszip_handle.release();
}

static uint8_t make_las_composite_0(laszip_point *point)
{
  return uint8_t(point->return_number) | uint8_t(point->number_of_returns) << 3 | uint8_t(point->scan_direction_flag) << 6 | uint8_t(point->edge_of_flight_line) << 7;
}

static uint8_t make_las_composite_1(laszip_point *point)
{
  return uint8_t(point->extended_return_number) | uint8_t(point->extended_number_of_returns) << 4;
}

static uint8_t make_las_composite_2(laszip_point *point)
{
  return uint8_t(point->extended_classification) | uint8_t(point->extended_scanner_channel) << 4 | uint8_t(point->scan_direction_flag) << 6 | uint8_t(point->edge_of_flight_line) << 7;
}

static uint8_t make_classification(laszip_point *point)
{
  return uint8_t(point->classification) | uint8_t(point->synthetic_flag) << 5 | uint8_t(point->keypoint_flag) << 6 | uint8_t(point->withheld_flag) << 7;
}

template <size_t FORMAT>
static void copy_point_for_format(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  assert(false);
  (void) buffers;
  (void) i;
  (void) point;
  // default should never be instansiated
}

void assert_copy(buffer_t &buffer, uint64_t i, size_t size, void *source)
{
  assert(static_cast<uint8_t *>(buffer.data) + i * size < static_cast<uint8_t *>(buffer.data) + buffer.size);
  assert(static_cast<uint8_t *>(buffer.data) + i * size + size <= static_cast<uint8_t *>(buffer.data) + buffer.size);
  memcpy(static_cast<uint8_t *>(buffer.data) + i * size, source, size);
}

template <>
inline void copy_point_for_format<0>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  assert_copy(buffers[0], i, sizeof(uint32_t[3]), &point->X);
  assert_copy(buffers[1], i, sizeof(uint16_t), &point->intensity);
  *(static_cast<uint8_t *>(buffers[2].data) + i * sizeof(uint8_t)) = make_las_composite_0(point);
  *(static_cast<uint8_t *>(buffers[3].data) + i * sizeof(uint8_t)) = make_classification(point);
  assert_copy(buffers[4], i, sizeof(int8_t), &point->scan_angle_rank);
  assert_copy(buffers[5], i, sizeof(uint8_t), &point->user_data);
  assert_copy(buffers[6], i, sizeof(uint16_t), &point->point_source_ID);
}

template <>
inline void copy_point_for_format<1>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<0>(buffers, i, point);
  assert_copy(buffers[7], i, sizeof(double), &point->gps_time);
}

template <>
inline void copy_point_for_format<2>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<0>(buffers, i, point);
  assert_copy(buffers[7], i, sizeof(uint16_t[3]), point->rgb);
}

template <>
inline void copy_point_for_format<3>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<1>(buffers, i, point);
  assert_copy(buffers[8], i, sizeof(uint16_t[3]), &point->rgb);
}

template <size_t OFFSET>
inline void copy_wave_packet(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  assert_copy(buffers[OFFSET], i, sizeof(uint8_t), &point->wave_packet);
  assert_copy(buffers[OFFSET + 1], i, sizeof(uint64_t), &point->wave_packet + sizeof(uint8_t));
  assert_copy(buffers[OFFSET + 2], i, sizeof(uint32_t), &point->wave_packet + sizeof(uint8_t) + sizeof(uint64_t));
  assert_copy(buffers[OFFSET + 3], i, sizeof(float), &point->wave_packet + sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t));
  assert_copy(buffers[OFFSET + 4], i, sizeof(float[3]), &point->wave_packet + sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t) + sizeof(float));
}

template <>
inline void copy_point_for_format<4>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<1>(buffers, i, point);
  copy_wave_packet<8>(buffers, i, point);
}

template <>
inline void copy_point_for_format<5>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<3>(buffers, i, point);
  copy_wave_packet<9>(buffers, i, point);
}

template <>
inline void copy_point_for_format<6>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  assert_copy(buffers[0], i, sizeof(uint32_t[3]), &point->X);
  assert_copy(buffers[1], i, sizeof(uint16_t), &point->intensity);
  *(static_cast<uint8_t *>(buffers[2].data) + i * sizeof(uint8_t)) = make_las_composite_1(point);
  *(static_cast<uint8_t *>(buffers[3].data) + i * sizeof(uint8_t)) = make_las_composite_2(point);
  assert_copy(buffers[4], i, sizeof(int8_t), &point->extended_classification);
  assert_copy(buffers[5], i, sizeof(uint8_t), &point->user_data);
  assert_copy(buffers[6], i, sizeof(uint16_t), &point->extended_scan_angle);
  assert_copy(buffers[7], i, sizeof(uint16_t), &point->point_source_ID);
  assert_copy(buffers[8], i, sizeof(double), &point->gps_time);
}

template <>
inline void copy_point_for_format<7>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<6>(buffers, i, point);
  assert_copy(buffers[9], i, sizeof(uint16_t[3]), &point->rgb);
}

template <>
inline void copy_point_for_format<8>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<7>(buffers, i, point);
  assert_copy(buffers[10], i, sizeof(uint16_t[3]), &point->rgb);
}

template <>
inline void copy_point_for_format<9>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<6>(buffers, i, point);
  copy_wave_packet<9>(buffers, i, point);
}

template <>
inline void copy_point_for_format<10>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<7>(buffers, i, point);
  copy_wave_packet<10>(buffers, i, point);
}

template <size_t FORMAT>
void copy_points_for_format(laszip_handle_t *laszip_handle, uint64_t point_count_to_stop_at, buffer_t *buffers, uint64_t buffers_size, struct error_t **error)
{
  (void)buffers_size;
  (void)buffers;
  auto point = laszip_handle->point;
  (void)point;
  for (uint64_t i = 0; laszip_handle->point_read < point_count_to_stop_at; laszip_handle->point_read++, i++)
  {
    if (laszip_read_point(laszip_handle->reader))
    {
      *error = new error_t();
      auto e = *error;
      e->code = -1;
      e->msg = fmt::format("Failed to read point from laszip reader '{}'.", laszip_handle->filename);
      return;
    }
    copy_point_for_format<FORMAT>(buffers, i, point);
  }
}

static void laszip_converter_file_convert_data(void *user_ptr, const header_t *header, const attribute_t *attributes, uint32_t attributes_size, uint32_t max_points_to_convert, buffer_t *buffers, uint32_t buffers_size,
                                               uint32_t *points_read, uint8_t *done, struct error_t **error)
{
  (void)header;
  (void)attributes;
  (void)attributes_size;
  laszip_handle_t *laszip_handle = static_cast<laszip_handle_t *>(user_ptr);
  uint32_t points_to_read;
  uint64_t points_left = laszip_handle->point_count - laszip_handle->point_read;
  if (points_left < max_points_to_convert)
  {
    points_to_read = uint32_t(points_left);
    *done = 1;
  }
  else
  {
    points_to_read = max_points_to_convert;
    *done = 0;
  }
  uint64_t point_count_to_stop_at = laszip_handle->point_read + points_to_read;
  switch (laszip_handle->las_format)
  {
  case 0:
    copy_points_for_format<0>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 1:
    copy_points_for_format<1>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 2:
    copy_points_for_format<2>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 3:
    copy_points_for_format<3>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 4:
    copy_points_for_format<4>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 5:
    copy_points_for_format<5>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 6:
    copy_points_for_format<6>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 7:
    copy_points_for_format<7>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 8:
    copy_points_for_format<8>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 9:
    copy_points_for_format<9>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  case 10:
    copy_points_for_format<10>(laszip_handle, point_count_to_stop_at, buffers, buffers_size, error);
    break;
  default:
    assert(false);
  }
  *points_read = points_to_read;
}

static void laszip_converter_file_destroy_user_ptr(void *user_ptr)
{
  laszip_handle_t *laszip_handle = static_cast<laszip_handle_t *>(user_ptr);
  delete laszip_handle;
}

struct converter_file_convert_callbacks_t laszip_callbacks()
{
  converter_file_convert_callbacks_t ret;
  ret.pre_init = &laszip_converter_file_get_aabb_min;
  ret.init = &laszip_converter_file_init;
  ret.convert_data = &laszip_converter_file_convert_data;
  ret.destroy_user_ptr = &laszip_converter_file_destroy_user_ptr;
  return ret;
}
} // namespace points::converter

