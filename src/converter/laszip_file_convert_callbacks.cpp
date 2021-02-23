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
#include <points/converter/laszip_file_convert_callbacks.h>
#include <points/converter/default_attribute_names.h>
#include <points/converter/converter.h>

#include "error_p.h"

#include <fmt/format.h>
#include <laszip/laszip_api.h>

#include <assert.h>
namespace points
{
namespace converter
{

struct laszip_handle_t
{
  std::string filename;
  laszip_POINTER reader;
  laszip_point *point = nullptr;
  uint64_t point_count = 0;
  uint64_t point_read = 0;
  uint8_t las_format;
};

struct laszip_POINTER_DELETER
{
  laszip_POINTER ptr;
  laszip_POINTER_DELETER(laszip_POINTER ptr)
    : ptr(ptr)
  {
  }
  ~laszip_POINTER_DELETER()
  {
    if (ptr)
    {
      if (laszip_close_reader(ptr))
      {
        fprintf(stderr, "Failed to close laszip reader\n");
      }

      if (laszip_destroy(ptr))
      {
        fprintf(stderr, "Failed to destroy laszip reader\n");
      }
    }
  }
};

template<size_t N>
void add_attribute(header_t *header, const char (&name)[N], format_t format, components_t components, int group = 0)
{
  header_add_attribute(header, name, N - 1, format, components, group);
}

static void add_attributes_format_0(header_t *header)
{
  add_attribute(header, POINTS_ATTRIBUTE_XYZ, format_i32, components_3);
  add_attribute(header, POINTS_ATTRIBUTE_INTENSITY, format_u16, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_LAS_COMPOSITE_0, format_u8, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_CLASSIFICATION, format_u8, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_SCAN_ANGLE_RANK, format_i8, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_USER_DATA, format_u8, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_POINT_SOURCE_ID, format_u16, components_1);
}

static void add_attributes_format_1(header_t *header)
{
  add_attributes_format_0(header);
  add_attribute(header, POINTS_ATTRIBUTE_GPS_TIME, format_r64, components_1);
}

static void add_attributes_format_2(header_t *header)
{
  add_attributes_format_0(header);
  add_attribute(header, POINTS_ATTRIBUTE_RGB, format_u16, components_3);
}

static void add_attributes_format_3(header_t *header)
{
  add_attributes_format_1(header);
  add_attribute(header, POINTS_ATTRIBUTE_RGB, format_u16, components_3);
}

static void add_wave_packets(header_t *header)
{
  add_attribute(header, POINTS_ATTRIBUTE_WAVE_PACKET_DESCRIPTOR_INDEX, format_u8, components_1, -1);
  add_attribute(header, POINTS_ATTRIBUTE_BYTE_OFFSET_TO_WAVEFORM_DATA, format_u64, components_1, -1);
  add_attribute(header, POINTS_ATTRIBUTE_WAVEFORM_PACKET_SIZE_BYTES, format_u32, components_1, -1);
  add_attribute(header, POINTS_ATTRIBUTE_RETURN_POINT_WAVEFORM_LOCATION, format_r32, components_1, -1);
  add_attribute(header, POINTS_ATTRIBUTE_XYZ_T, format_r32, components_3, -1);
}

static void add_attributes_format_4(header_t *header)
{
  add_attributes_format_1(header);
  add_wave_packets(header);
}

static void add_attributes_format_5(header_t *header)
{
  add_attributes_format_3(header);
  add_wave_packets(header);
}

static void add_attributes_format_6(header_t *header)
{
  add_attribute(header, POINTS_ATTRIBUTE_XYZ, format_i32, components_3);
  add_attribute(header, POINTS_ATTRIBUTE_INTENSITY, format_u16, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_LAS_COMPOSITE_1, format_u8, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_LAS_COMPOSITE_2, format_u8, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_CLASSIFICATION, format_u8, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_USER_DATA, format_u8, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_SCAN_ANGLE, format_i16, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_POINT_SOURCE_ID, format_u16, components_1);
  add_attribute(header, POINTS_ATTRIBUTE_GPS_TIME, format_r64, components_1);
}

static void add_attributes_format_7(header_t *header)
{
  add_attributes_format_6(header);
  add_attribute(header, POINTS_ATTRIBUTE_RGB, format_u16, components_3);
}

static void add_attributes_format_8(header_t *header)
{
  add_attributes_format_7(header);
  add_attribute(header, POINTS_ATTRIBUTE_NEAR_INFRARED, format_u16, components_1);
}

static void add_attributes_format_9(header_t *header)
{
  add_attributes_format_6(header);
  add_wave_packets(header);
}

static void add_attributes_format_10(header_t *header)
{
  add_attributes_format_7(header);
  add_wave_packets(header);
}

static void laszip_converter_file_init(const char *filename, size_t filename_size, header_t *header, void **user_ptr, struct error_t **error)
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

  laszip_POINTER_DELETER deleter(laszip_handle.get());

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
  header_set_point_count(header, laszip_handle->point_count);
  double offset[3] = {lasheader->x_offset, lasheader->y_offset, lasheader->z_offset};
  double scale[3] = {lasheader->x_scale_factor, lasheader->y_scale_factor, lasheader->z_scale_factor};
  header_set_coordinate_offset(header, offset);  
  header_set_coordinate_scale(header, scale);
 
  double min[3] = {lasheader->min_x, lasheader->min_y, lasheader->min_z};
  double max[3] = {lasheader->max_x, lasheader->max_y, lasheader->max_z};
  header_set_aabb(header, min, max);

  laszip_handle->las_format = lasheader->point_data_format;
  switch (lasheader->point_data_format)
  {
  case 0:
    add_attributes_format_0(header);
    break;
  case 1:
    add_attributes_format_1(header);
    break;
  case 2:
    add_attributes_format_2(header);
    break;
  case 3:
    add_attributes_format_3(header);
    break;
  case 4:
    add_attributes_format_4(header);
    break;
  case 5:
    add_attributes_format_5(header);
    break;
  case 6:
    add_attributes_format_6(header);
    break;
  case 7:
    add_attributes_format_7(header);
    break;
  case 8:
    add_attributes_format_8(header);
    break;
  case 9:
    add_attributes_format_9(header);
    break;
  case 10:
    add_attributes_format_10(header);
    break;
  default:
    assert(false);
  }


  *user_ptr = laszip_handle.get();
  deleter.ptr = nullptr;
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

template<size_t FORMAT>
static void copy_point_for_format(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  assert(false);
  //default should never be instansiated
}

template<>
inline void copy_point_for_format<0>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  memcpy(static_cast<uint8_t *>(buffers[0].data) + i * sizeof(uint32_t[3]), &point->X, sizeof(uint32_t[3]));
  memcpy(static_cast<uint8_t *>(buffers[1].data) + i * sizeof(uint16_t), &point->intensity, sizeof(uint16_t));
  *(static_cast<uint8_t *>(buffers[2].data) + i * sizeof(uint8_t)) = make_las_composite_0(point);
  *(static_cast<uint8_t *>(buffers[3].data) + i * sizeof(uint8_t)) = make_classification(point);
  memcpy(static_cast<uint8_t *>(buffers[4].data) + i * sizeof(int8_t), &point->scan_angle_rank, sizeof(int8_t));
  memcpy(static_cast<uint8_t *>(buffers[5].data) + i * sizeof(uint8_t), &point->user_data, sizeof(uint8_t));
  memcpy(static_cast<uint8_t *>(buffers[6].data) + i * sizeof(uint16_t), &point->point_source_ID, sizeof(uint16_t));
}

template<>
inline void copy_point_for_format<1>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<0>(buffers, i, point);
  memcpy(static_cast<uint8_t *>(buffers[7].data) + i * sizeof(double), &point->gps_time, sizeof(double));
}

template<>
inline void copy_point_for_format<2>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<0>(buffers, i, point);
  memcpy(static_cast<uint8_t *>(buffers[7].data) + i * sizeof(uint16_t[3]), &point->rgb, sizeof(uint16_t[3]));
}

template<>
inline void copy_point_for_format<3>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<1>(buffers, i, point);
  memcpy(static_cast<uint8_t *>(buffers[8].data) + i * sizeof(uint16_t[3]), &point->rgb, sizeof(uint16_t[3]));
}

template<size_t OFFSET>
inline void copy_wave_packet(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  memcpy(static_cast<uint8_t *>(buffers[OFFSET].data) + i * sizeof(uint8_t), &point->wave_packet, sizeof(uint8_t));
  memcpy(static_cast<uint8_t *>(buffers[OFFSET+1].data) + i * sizeof(uint64_t), point->wave_packet + sizeof(uint8_t), sizeof(uint64_t));
  memcpy(static_cast<uint8_t *>(buffers[OFFSET+2].data) + i * sizeof(uint32_t), point->wave_packet + sizeof(uint8_t) + sizeof(uint64_t), sizeof(uint32_t));
  memcpy(static_cast<uint8_t *>(buffers[OFFSET+3].data) + i * sizeof(float), point->wave_packet + sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t), sizeof(float));
  memcpy(static_cast<uint8_t *>(buffers[OFFSET+4].data) + i * sizeof(float[3]), point->wave_packet + sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t) + sizeof(float), sizeof(float[3]));
}

template<>
inline void copy_point_for_format<4>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<1>(buffers, i, point);
  copy_wave_packet<8>(buffers, i, point);
}

template<>
inline void copy_point_for_format<5>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<3>(buffers, i, point);
  copy_wave_packet<9>(buffers, i, point);
}

template<>
inline void copy_point_for_format<6>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  memcpy(static_cast<uint8_t *>(buffers[0].data) + i * sizeof(uint32_t[3]), &point->X, sizeof(uint32_t[3]));
  memcpy(static_cast<uint8_t *>(buffers[1].data) + i * sizeof(uint16_t), &point->intensity, sizeof(uint16_t));
  *(static_cast<uint8_t *>(buffers[2].data) + i * sizeof(uint8_t)) = make_las_composite_1(point);
  *(static_cast<uint8_t *>(buffers[3].data) + i * sizeof(uint8_t)) = make_las_composite_2(point);
  memcpy(static_cast<uint8_t *>(buffers[4].data) + i * sizeof(int8_t), &point->extended_classification, sizeof(int8_t));
  memcpy(static_cast<uint8_t *>(buffers[5].data) + i * sizeof(uint8_t), &point->user_data, sizeof(uint8_t));
  memcpy(static_cast<uint8_t *>(buffers[6].data) + i * sizeof(uint16_t), &point->extended_scan_angle, sizeof(uint16_t));
  memcpy(static_cast<uint8_t *>(buffers[7].data) + i * sizeof(uint16_t), &point->point_source_ID, sizeof(uint16_t));
  memcpy(static_cast<uint8_t *>(buffers[8].data) + i * sizeof(double), &point->gps_time, sizeof(double));
}

template<>
inline void copy_point_for_format<7>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<6>(buffers, i, point);
  memcpy(static_cast<uint8_t *>(buffers[9].data) + i * sizeof(uint16_t[3]), &point->rgb, sizeof(uint16_t[3]));
}

template<>
inline void copy_point_for_format<8>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<7>(buffers, i, point);
  memcpy(static_cast<uint8_t *>(buffers[10].data) + i * sizeof(uint16_t), &point->rgb[3], sizeof(uint16_t));
}

template<>
inline void copy_point_for_format<9>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<6>(buffers, i, point);
  copy_wave_packet<9>(buffers, i, point);
}

template<>
inline void copy_point_for_format<10>(buffer_t *buffers, uint64_t i, laszip_point *point)
{
  copy_point_for_format<7>(buffers, i, point);
  copy_wave_packet<10>(buffers, i, point);
}

template<size_t FORMAT>
void copy_points_for_format(laszip_handle_t *laszip_handle, uint64_t point_count_to_stop_at, buffer_t *buffers, uint64_t buffers_size, struct error_t **error)
{
  (void)buffers_size;
  auto point = laszip_handle->point;
  for (uint64_t i = 0; laszip_handle->point_read < point_count_to_stop_at; laszip_handle->point_read, i++)
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

static uint64_t laszip_converter_file_convert_data(void *user_ptr, const header_t *header, const attribute_t *attributes, uint64_t attributes_size, buffer_t *buffers, uint64_t buffers_size, uint64_t max_points_to_convert, struct error_t **error)
{
  (void)header;
  (void)attributes;
  (void)attributes_size;
  laszip_handle_t *laszip_handle = static_cast<laszip_handle_t *>(user_ptr);
  uint64_t points_to_read = std::min(max_points_to_convert, laszip_handle->point_count - laszip_handle->point_read);
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
  return points_to_read;
}

static void laszip_converter_file_destroy_user_ptr(void *user_ptr)
{
  laszip_handle_t *laszip_handle = static_cast<laszip_handle_t *>(user_ptr);
  delete laszip_handle;
}

struct converter_file_convert_callbacks_t laszip_callbacks()
{
  converter_file_convert_callbacks_t ret;
  ret.init = &laszip_converter_file_init;
  ret.convert_data = &laszip_converter_file_convert_data;
  ret.destroy_user_ptr = &laszip_converter_file_destroy_user_ptr;
  return ret;
}
}
} // namespace points
