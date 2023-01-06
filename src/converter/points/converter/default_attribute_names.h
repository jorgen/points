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
#ifndef POINTS_DEFAULT_ATTRIBUTE_NAMES_H
#define POINTS_DEFAULT_ATTRIBUTE_NAMES_H

#define POINTS_ATTRIBUTE_XYZ "xyz"
#define POINTS_ATTRIBUTE_INTENSITY "intensity"

// | Return Number 3 bits (bits 0 - 2) | Number of Returns (given pulse) 3 bits (bits 3 - 5) | Scan Direction Flag 1 bit (bit 6) | Edge of Flight Line 1 bit (bit 7) |
#define POINTS_ATTRIBUTE_LAS_COMPOSITE_0 "las_composite_0"
// | Return Number 4 bits(bits 0 - 3)  | Number of Returns(given pulse) 4 bits(bits 4 - 7)   |
#define POINTS_ATTRIBUTE_LAS_COMPOSITE_1 "las_composite_1"
// | Classification Flags 4 bits(bits 0 - 3) | Scanner Channel 2 bits(bits 4 - 5) | Scan Direction Flag 1 bit(bit 6) | Edge of Flight Line 1 bit(bit 7) |
#define POINTS_ATTRIBUTE_LAS_COMPOSITE_2 "las_composite_2"

#define POINTS_ATTRIBUTE_CLASSIFICATION "classification"
#define POINTS_ATTRIBUTE_SCAN_ANGLE_RANK "scan_angle_rank"
#define POINTS_ATTRIBUTE_USER_DATA "user_data"
#define POINTS_ATTRIBUTE_POINT_SOURCE_ID "point_source_id"
#define POINTS_ATTRIBUTE_GPS_TIME "gps_time"
#define POINTS_ATTRIBUTE_RGB "rgb"

//leaving the wave packet stuff as is, but this needs to change somehow
#define POINTS_ATTRIBUTE_WAVE_PACKET_DESCRIPTOR_INDEX "wave_packet_descriptor_index"
#define POINTS_ATTRIBUTE_BYTE_OFFSET_TO_WAVEFORM_DATA "byte_offset_to_waveform_data"
#define POINTS_ATTRIBUTE_WAVEFORM_PACKET_SIZE_BYTES "waveform_packet_size_bytes"
#define POINTS_ATTRIBUTE_RETURN_POINT_WAVEFORM_LOCATION "return_point_waveform_location"
#define POINTS_ATTRIBUTE_XYZ_T "xyz_t"

#define POINTS_ATTRIBUTE_SCAN_ANGLE "scan_angle"
#define POINTS_ATTRIBUTE_NEAR_INFRARED "near_infrared"

//#define POINTS_ATTRIBUTE_RETURN_NUMBER_NUMBER_OF_RETURNES_SCAN_DIR_EDGE_OF_FLIGHT ""
//  laszip_U8 number_of_returns : 3;
//  laszip_U8 scan_direction_flag : 1;
//  laszip_U8 edge_of_flight_line : 1;
//  laszip_U8 classification : 5;
//  laszip_U8 synthetic_flag : 1;
//  laszip_U8 keypoint_flag  : 1;
//  laszip_U8 withheld_flag  : 1;
//  laszip_I8 scan_angle_rank;
//  laszip_U8 user_data;
//  laszip_U16 point_source_ID;
//
//  laszip_I16 extended_scan_angle;
//  laszip_U8 extended_point_type : 2;
//  laszip_U8 extended_scanner_channel : 2;
//  laszip_U8 extended_classification_flags : 4;
//  laszip_U8 extended_classification;
//  laszip_U8 extended_return_number : 4;
//  laszip_U8 extended_number_of_returns : 4;
//
//  laszip_U8 dummy[7];
//
//  laszip_F64 gps_time;
//  laszip_U16 rgb[4];
//  laszip_U8 wave_packet[29];
//
//  laszip_I32 num_extra_bytes;
//  laszip_U8* extra_bytes;
#endif
