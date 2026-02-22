/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jorgen Lind
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
#pragma once

#include <cstdint>
#include <vector>

namespace points::converter
{

// Delta encoding for morton codes (in-place).
// Returns true if encoding was applied (data is monotonically non-decreasing).
// Returns false without modifying data if any value decreases.
bool delta_encode_morton(uint8_t *data, uint32_t size, uint8_t type_size);

// Delta decoding (prefix sum, in-place).
void delta_decode_morton(uint8_t *data, uint32_t size, uint8_t type_size);

struct band_compact_result_t
{
  std::vector<uint8_t> compacted_data; // non-constant bands + remainder
  uint32_t band_mask;                  // bit i set = band i is constant
  std::vector<uint8_t> constant_values; // one byte per set bit in band_mask
};

// Detect constant bands in byte-shuffled data.
// Returns compacted data with constant bands removed.
band_compact_result_t detect_constant_bands(const uint8_t *shuffled, uint32_t size, uint8_t type_size, uint8_t component_count);

// Restore constant bands from compacted data back to full byte-shuffled layout.
void restore_constant_bands(const uint8_t *compacted, uint32_t compacted_size, uint8_t *shuffled, uint32_t shuffled_size, uint32_t band_mask, const uint8_t *constant_values, uint8_t type_size, uint8_t component_count);

// Offset subtraction for f64 values: subtract the minimum from all values.
// Returns the minimum value that was subtracted.
double offset_subtract_f64(uint8_t *data, uint32_t size);

// Offset restoration for f64 values: add min_value back to all values.
void offset_restore_f64(uint8_t *data, uint32_t size, double min_value);

// Sort f64 data (treated as uint64_t for positive-double ordering) and output a u16 permutation.
// Returns true if sorting was applied. perm_out must be pre-allocated with size/8 entries.
// Only works when element count fits in u16 (<=65535).
bool sort_with_permutation_f64(uint8_t *data, uint32_t size, uint16_t *perm_out);

// Restore original order of f64 data using the stored permutation.
void unsort_with_permutation_f64(uint8_t *data, uint32_t size, const uint16_t *perm);

// RGB decorrelation: transform interleaved (R,G,B) u16 triples to (G, R-G, B-G).
// Operates in-place on raw interleaved data before byte_shuffle.
void decorrelate_u16x3(uint8_t *data, uint32_t size);

// RGB correlation: reverse decorrelation, transform (G, dR, dB) to (dR+G, G, dB+G).
// Operates in-place on raw interleaved data after byte_unshuffle.
void correlate_u16x3(uint8_t *data, uint32_t size);

// Portable popcount.
inline uint32_t popcount32(uint32_t x)
{
#if defined(_MSC_VER)
  return __popcnt(x);
#else
  return static_cast<uint32_t>(__builtin_popcount(x));
#endif
}

} // namespace points::converter
