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
#include "compression_preprocess.hpp"

#include <cstring>

namespace points::converter
{

// Multi-word comparison: returns true if a < b (MSW-first, matching morton.hpp operator<).
// word_count = type_size / 8 for 64-bit word types, or type_size / 4 for 32-bit.
// data layout: data[0] = LSW (matching morton_t::data[0]).

static bool less_than_u32(const uint8_t *a, const uint8_t *b)
{
  uint32_t va, vb;
  memcpy(&va, a, 4);
  memcpy(&vb, b, 4);
  return va < vb;
}

static bool less_than_u64(const uint8_t *a, const uint8_t *b)
{
  uint64_t va, vb;
  memcpy(&va, a, 8);
  memcpy(&vb, b, 8);
  return va < vb;
}

static bool less_than_u128(const uint8_t *a, const uint8_t *b)
{
  // 2x uint64_t, data[1] is MSW
  uint64_t a0, a1, b0, b1;
  memcpy(&a0, a, 8);
  memcpy(&a1, a + 8, 8);
  memcpy(&b0, b, 8);
  memcpy(&b1, b + 8, 8);
  if (a1 != b1)
    return a1 < b1;
  return a0 < b0;
}

static bool less_than_u192(const uint8_t *a, const uint8_t *b)
{
  // 3x uint64_t, data[2] is MSW
  uint64_t a0, a1, a2, b0, b1, b2;
  memcpy(&a0, a, 8);
  memcpy(&a1, a + 8, 8);
  memcpy(&a2, a + 16, 8);
  memcpy(&b0, b, 8);
  memcpy(&b1, b + 8, 8);
  memcpy(&b2, b + 16, 8);
  if (a2 != b2)
    return a2 < b2;
  if (a1 != b1)
    return a1 < b1;
  return a0 < b0;
}

// Multi-word subtraction: result = a - b, stored in dst. Unsigned wrapping.
static void sub_u32(const uint8_t *a, const uint8_t *b, uint8_t *dst)
{
  uint32_t va, vb;
  memcpy(&va, a, 4);
  memcpy(&vb, b, 4);
  uint32_t r = va - vb;
  memcpy(dst, &r, 4);
}

static void sub_u64(const uint8_t *a, const uint8_t *b, uint8_t *dst)
{
  uint64_t va, vb;
  memcpy(&va, a, 8);
  memcpy(&vb, b, 8);
  uint64_t r = va - vb;
  memcpy(dst, &r, 8);
}

static void sub_u128(const uint8_t *a, const uint8_t *b, uint8_t *dst)
{
  uint64_t a0, a1, b0, b1;
  memcpy(&a0, a, 8);
  memcpy(&a1, a + 8, 8);
  memcpy(&b0, b, 8);
  memcpy(&b1, b + 8, 8);
  uint64_t r0 = a0 - b0;
  uint64_t borrow = (a0 < b0) ? 1 : 0;
  uint64_t r1 = a1 - b1 - borrow;
  memcpy(dst, &r0, 8);
  memcpy(dst + 8, &r1, 8);
}

static void sub_u192(const uint8_t *a, const uint8_t *b, uint8_t *dst)
{
  uint64_t a0, a1, a2, b0, b1, b2;
  memcpy(&a0, a, 8);
  memcpy(&a1, a + 8, 8);
  memcpy(&a2, a + 16, 8);
  memcpy(&b0, b, 8);
  memcpy(&b1, b + 8, 8);
  memcpy(&b2, b + 16, 8);
  uint64_t r0 = a0 - b0;
  uint64_t borrow = (a0 < b0) ? 1 : 0;
  uint64_t r1 = a1 - b1 - borrow;
  borrow = (a1 < b1 + borrow || (borrow && b1 == UINT64_MAX)) ? 1 : 0;
  uint64_t r2 = a2 - b2 - borrow;
  memcpy(dst, &r0, 8);
  memcpy(dst + 8, &r1, 8);
  memcpy(dst + 16, &r2, 8);
}

// Multi-word addition: result = a + b, stored in dst. Unsigned wrapping.
static void add_u32(const uint8_t *a, const uint8_t *b, uint8_t *dst)
{
  uint32_t va, vb;
  memcpy(&va, a, 4);
  memcpy(&vb, b, 4);
  uint32_t r = va + vb;
  memcpy(dst, &r, 4);
}

static void add_u64(const uint8_t *a, const uint8_t *b, uint8_t *dst)
{
  uint64_t va, vb;
  memcpy(&va, a, 8);
  memcpy(&vb, b, 8);
  uint64_t r = va + vb;
  memcpy(dst, &r, 8);
}

static void add_u128(const uint8_t *a, const uint8_t *b, uint8_t *dst)
{
  uint64_t a0, a1, b0, b1;
  memcpy(&a0, a, 8);
  memcpy(&a1, a + 8, 8);
  memcpy(&b0, b, 8);
  memcpy(&b1, b + 8, 8);
  uint64_t r0 = a0 + b0;
  uint64_t carry = (r0 < a0) ? 1 : 0;
  uint64_t r1 = a1 + b1 + carry;
  memcpy(dst, &r0, 8);
  memcpy(dst + 8, &r1, 8);
}

static void add_u192(const uint8_t *a, const uint8_t *b, uint8_t *dst)
{
  uint64_t a0, a1, a2, b0, b1, b2;
  memcpy(&a0, a, 8);
  memcpy(&a1, a + 8, 8);
  memcpy(&a2, a + 16, 8);
  memcpy(&b0, b, 8);
  memcpy(&b1, b + 8, 8);
  memcpy(&b2, b + 16, 8);
  uint64_t r0 = a0 + b0;
  uint64_t carry = (r0 < a0) ? 1 : 0;
  uint64_t r1 = a1 + b1 + carry;
  carry = (r1 < a1 || (carry && r1 == a1)) ? 1 : 0;
  uint64_t r2 = a2 + b2 + carry;
  memcpy(dst, &r0, 8);
  memcpy(dst + 8, &r1, 8);
  memcpy(dst + 16, &r2, 8);
}

using less_fn = bool (*)(const uint8_t *, const uint8_t *);
using arith_fn = void (*)(const uint8_t *, const uint8_t *, uint8_t *);

struct morton_ops_t
{
  less_fn less;
  arith_fn sub;
  arith_fn add;
};

static morton_ops_t get_morton_ops(uint8_t type_size)
{
  switch (type_size)
  {
  case 4:
    return {less_than_u32, sub_u32, add_u32};
  case 8:
    return {less_than_u64, sub_u64, add_u64};
  case 16:
    return {less_than_u128, sub_u128, add_u128};
  case 24:
    return {less_than_u192, sub_u192, add_u192};
  default:
    return {nullptr, nullptr, nullptr};
  }
}

bool delta_encode_morton(uint8_t *data, uint32_t size, uint8_t type_size)
{
  if (type_size == 0 || size < type_size)
    return false;

  auto ops = get_morton_ops(type_size);
  if (!ops.less)
    return false;

  uint32_t count = size / type_size;
  if (count <= 1)
    return true;

  // Verify monotonically non-decreasing
  for (uint32_t i = 1; i < count; i++)
  {
    const uint8_t *prev = data + (i - 1) * type_size;
    const uint8_t *curr = data + i * type_size;
    if (ops.less(curr, prev))
      return false;
  }

  // Delta encode back-to-front to avoid clobbering
  for (uint32_t i = count - 1; i > 0; i--)
  {
    uint8_t *curr = data + i * type_size;
    const uint8_t *prev = data + (i - 1) * type_size;
    ops.sub(curr, prev, curr);
  }

  return true;
}

void delta_decode_morton(uint8_t *data, uint32_t size, uint8_t type_size)
{
  if (type_size == 0 || size < type_size)
    return;

  auto ops = get_morton_ops(type_size);
  if (!ops.add)
    return;

  uint32_t count = size / type_size;

  // Prefix sum front-to-back
  for (uint32_t i = 1; i < count; i++)
  {
    uint8_t *curr = data + i * type_size;
    const uint8_t *prev = data + (i - 1) * type_size;
    ops.add(curr, prev, curr);
  }
}

band_compact_result_t detect_constant_bands(const uint8_t *shuffled, uint32_t size, uint8_t type_size, uint8_t component_count)
{
  band_compact_result_t result;
  result.band_mask = 0;

  uint32_t stride = static_cast<uint32_t>(type_size) * static_cast<uint32_t>(component_count);
  if (stride == 0 || size == 0)
  {
    result.compacted_data.assign(shuffled, shuffled + size);
    return result;
  }

  uint32_t element_count = size / stride;
  uint32_t remainder = size % stride;
  uint32_t band_count = stride; // type_size * component_count bands

  // Graceful degradation: if too many bands for the mask, skip compaction
  if (band_count > 32 || element_count == 0)
  {
    result.compacted_data.assign(shuffled, shuffled + size);
    return result;
  }

  // Scan each band to check if all bytes are the same value
  for (uint32_t b = 0; b < band_count; b++)
  {
    const uint8_t *band_start = shuffled + b * element_count;
    uint8_t first_val = band_start[0];
    bool is_constant = true;
    for (uint32_t i = 1; i < element_count; i++)
    {
      if (band_start[i] != first_val)
      {
        is_constant = false;
        break;
      }
    }
    if (is_constant)
    {
      result.band_mask |= (1u << b);
      result.constant_values.push_back(first_val);
    }
  }

  if (result.band_mask == 0)
  {
    // No constant bands found
    result.compacted_data.assign(shuffled, shuffled + size);
    return result;
  }

  // Build compacted data: non-constant bands + remainder
  uint32_t non_constant_bands = band_count - popcount32(result.band_mask);
  uint32_t compacted_size = non_constant_bands * element_count + remainder;
  result.compacted_data.resize(compacted_size);

  uint32_t dst_offset = 0;
  for (uint32_t b = 0; b < band_count; b++)
  {
    if (result.band_mask & (1u << b))
      continue;
    memcpy(result.compacted_data.data() + dst_offset, shuffled + b * element_count, element_count);
    dst_offset += element_count;
  }

  // Copy remainder
  if (remainder > 0)
  {
    memcpy(result.compacted_data.data() + dst_offset, shuffled + band_count * element_count, remainder);
  }

  return result;
}

void restore_constant_bands(const uint8_t *compacted, uint32_t compacted_size, uint8_t *shuffled, uint32_t shuffled_size, uint32_t band_mask, const uint8_t *constant_values, uint8_t type_size, uint8_t component_count)
{
  uint32_t stride = static_cast<uint32_t>(type_size) * static_cast<uint32_t>(component_count);
  if (stride == 0 || shuffled_size == 0)
    return;

  uint32_t element_count = shuffled_size / stride;
  uint32_t remainder = shuffled_size % stride;
  uint32_t band_count = stride;

  uint32_t src_offset = 0;
  uint32_t const_idx = 0;

  for (uint32_t b = 0; b < band_count; b++)
  {
    uint8_t *band_dst = shuffled + b * element_count;
    if (band_mask & (1u << b))
    {
      memset(band_dst, constant_values[const_idx], element_count);
      const_idx++;
    }
    else
    {
      memcpy(band_dst, compacted + src_offset, element_count);
      src_offset += element_count;
    }
  }

  // Copy remainder
  if (remainder > 0)
  {
    memcpy(shuffled + band_count * element_count, compacted + src_offset, remainder);
  }

  (void)compacted_size;
}

} // namespace points::converter
