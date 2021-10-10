#pragma once

#include <libmorton/morton.h>
#include <math.h>

#include <assert.h>

namespace points
{
namespace converter
{
namespace morton
{

template<typename T>
struct morton_t
{
  T data[3];
};

using morton64_t = morton_t<uint64_t>;

template<typename T>
bool operator==(const morton_t<T> &a, const morton_t<T> &b)
{
  return a.data[0] == b.data[0] && a.data[1] == b.data[1] && a.data[2] == b.data[2];
}

template<typename T>
bool operator<(const morton_t<T> &a, const morton_t<T> &b)
{
  if (a.data[2] == b.data[2])
  {
    if (a.data[1] == b.data[1])
    {
      return a.data[0] < b.data[0];
    }
    else
    {
      return a.data[1] < b.data[1];
    }
  }
  return a.data[2] < b.data[2];
}

template<typename T>
void morton_init_min(morton_t<T> &a)
{
  a.data[0] = 0;
  a.data[1] = 0;
  a.data[2] = 0;
}
template<typename T>
void morton_init_max(morton_t<T> &a)
{
  a.data[0] = ~T(0);
  a.data[1] = ~T(0);
  a.data[2] = ~T(0);
}

inline void encode(uint32_t *pos)
{
  uint64_t low = libmorton::morton3D_64_encode(pos[0], pos[1], pos[2]);
  uint64_t high = libmorton::morton3D_64_encode(pos[0] >> 22, pos[1] >> 21, pos[2] >> 21);
  memcpy(pos, &low, sizeof(low));
  pos[2] = uint32_t(high);
}
inline void encode(const uint32_t (&pos)[3], uint32_t (&morton)[3])
{
  uint64_t low = libmorton::morton3D_64_encode(pos[0], pos[1], pos[2]);
  uint64_t high = libmorton::morton3D_64_encode(pos[0] >> 22, pos[1] >> 21, pos[2] >> 21);
  memcpy(morton, &low, sizeof(low));
  morton[2] = uint32_t(high);
}
inline void encode(const uint32_t (&pos)[3], morton_t<uint32_t> &morton)
{
  encode(pos, morton.data);
}

inline void decode(uint32_t *morton)
{
  uint64_t low;
  memcpy(&low, morton, sizeof(low));
  uint32_t pos[3];
  libmorton::morton3D_64_decode(low, pos[0], pos[1], pos[2]);
  uint64_t high = morton[2];
  uint32_t high_pos[3];
  libmorton::morton3D_64_decode(high, high_pos[0], high_pos[1], high_pos[2]);
  morton[0] = pos[0] | high_pos[0] << 22;
  morton[1] = pos[1] | high_pos[1] << 21;
  morton[2] = pos[2] | high_pos[2] << 21;
}

inline void decode(const uint32_t (&morton)[3], uint32_t (&pos)[3])
{
  uint64_t low;
  memcpy(&low, morton, sizeof(low));
  libmorton::morton3D_64_decode(low, pos[0], pos[1], pos[2]);
  uint64_t high = morton[2];
  uint32_t high_pos[3];
  libmorton::morton3D_64_decode(high, high_pos[0], high_pos[1], high_pos[2]);
  pos[0] |= high_pos[0] << 22;
  pos[1] |= high_pos[1] << 21;
  pos[2] |= high_pos[2] << 21;
}

inline void decode(const morton_t<uint32_t> &morton, uint32_t (&pos)[3])
{
  decode(morton.data, pos);
}

inline void decode(const morton64_t &morton, uint64_t (&pos)[3])
{
  uint32_t lower[3];
  uint32_t mid[3];
  uint32_t high[3];

  libmorton::morton3D_64_decode(morton.data[0], lower[0], lower[1], lower[2]);
  libmorton::morton3D_64_decode(morton.data[1], mid[0], mid[1], mid[2]);
  libmorton::morton3D_64_decode(morton.data[2], high[0], high[1], high[2]);

  pos[0] = uint64_t(lower[0]) | uint64_t(mid[0]) << 22 | uint64_t(high[0]) << (22 + 21);
  pos[1] = uint64_t(lower[1]) | uint64_t(mid[1]) << 21 | uint64_t(high[1]) << (21 + 22);
  pos[2] = uint64_t(lower[2]) | uint64_t(mid[2]) << 21 | uint64_t(high[1]) << (21 + 21);
}


inline void encode(const uint64_t (&pos)[3], morton64_t &morton)
{
  constexpr uint32_t mask21 = (uint32_t(1) << 21) - 1;
  constexpr uint32_t mask22 = (uint32_t(1) << 22) - 1;

  uint32_t x_lower = pos[0] & mask22;
  uint32_t x_mid = (pos[0] >> 22) & mask21;
  uint32_t x_high = pos[0] >> (22 + 21);

  uint32_t y_lower = pos[1] & mask21;
  uint32_t y_mid = (pos[1] >> 21) & mask22;
  uint32_t y_high = pos[1] >> (21 + 22);

  uint32_t z_lower = pos[2] & mask21;
  uint32_t z_mid = (pos[2] >> 21) & mask21;
  uint32_t z_high = pos[2] >> (21 + 21);

  // X starts at LSB
  morton.data[0] = libmorton::morton3D_64_encode(x_lower, y_lower, z_lower);
  morton.data[1] = libmorton::morton3D_64_encode(x_mid, y_mid, z_mid);
  morton.data[2] = libmorton::morton3D_64_encode(x_high, y_high, z_high);
}

inline bool morton_lt(const morton64_t &a, const morton64_t &b)
{
  if (a.data[2] == b.data[2])
  {
    if (a.data[1] == b.data[1])
    {
      return a.data[0] < b.data[0];
    }
    else
    {
      return a.data[1] < b.data[1];
    }
  }
  return a.data[2] < b.data[2];
}

inline bool morton_gt(const morton64_t &a, const morton64_t &b)
{
  if (a.data[2] == b.data[2])
  {
    if (a.data[1] == b.data[1])
    {
      return a.data[0] > b.data[0];
    }
    else
    {
      return a.data[1] > b.data[1];
    }
  }
  return a.data[2] > b.data[2];
}

inline morton64_t morton_xor(const morton64_t &a, const morton64_t &b)
{
  morton64_t c;
  c.data[0] = a.data[0] ^ b.data[0];
  c.data[1] = a.data[1] ^ b.data[1];
  c.data[2] = a.data[2] ^ b.data[2];
  return c;
}

inline bool morton_null(const morton64_t &a)
{
  return a.data[0] == uint64_t(0) && a.data[1] == uint64_t(0) && a.data[2] == uint64_t(0);
}

inline morton64_t morton_or(const morton64_t &a, const morton64_t &b)
{
  morton64_t c;
  c.data[0] = a.data[0] | b.data[0];
  c.data[1] = a.data[1] | b.data[1];
  c.data[2] = a.data[2] | b.data[2];
  return c;
}

inline morton64_t morton_negate(const morton64_t &a)
{
  morton64_t b;
  b.data[0] = ~a.data[0];
  b.data[1] = ~a.data[1];
  b.data[2] = ~a.data[2];
  return b;
}

inline morton64_t morton_and(const morton64_t &a, const morton64_t &b)
{
  morton64_t c;
  c.data[0] = a.data[0] & b.data[0];
  c.data[1] = a.data[1] & b.data[1];
  c.data[2] = a.data[2] & b.data[2];
  return c;
}

inline void morton_add_one(morton64_t &a)
{
  if (a.data[0] == (~uint64_t(0)))
  {
    a.data[0] = 0;
    if (a.data[1] == (~uint64_t(0)))
    {
      a.data[1] = 0;
      a.data[2]++; 
    }
    else
    {
      a.data[1]++;
    }
  }
  a.data[0]++;
}

template<typename T>
inline morton_t<T> morton_add(const morton_t<T> &a, const morton_t<T> &b)
{
  morton_t<T> ret;
  ret.data[0] = a.data[0] + b.data[0];
  int carry = ret.data[0] < a.data[0]? 1 : 0;
  ret.data[1] = a.data[1] + b.data[1] + carry;
  carry = ret.data[1] < a.data[1]? 1 : 0;
  ret.data[2] = a.data[2] + b.data[2] + carry;
  return ret;
}

template<typename T>
inline bool morton_is_set(const morton_t<T> &a)
{
  return a.data[0] || a.data[1] || a.data[2];
}

static inline int bit_scan_reverse(uint64_t a)
{
#ifdef _MSC_VER
  unsigned long index;
#ifdef _WIN64
  _BitScanReverse64(&index, a);
#else
  if (_BitScanReverse(&index, a >> 32))
    index += 32;
  else
    _BitScanReverse(&index, a & (~uint32_t(0)));
#endif
  return int(index);
#else
  static_assert(sizeof(unsigned long long) == sizeof(uint64_t), "Wrong size for builtin_clzll");
  return 63 - __builtin_clzll(a);
#endif
}

inline int morton_msb(const morton64_t &a)
{
  if (a.data[2])
  {
    return 64 * 2 + bit_scan_reverse(a.data[2]);
  }
  else if (a.data[1])
  {
    return 64 * 1 + bit_scan_reverse(a.data[1]);
  }
  else if (a.data[0])
  {
    return 64 * 0 + bit_scan_reverse(a.data[0]);
  }
  return 0;
}

inline int morton_lod_from_bit_index(int index)
{
  return index / 3;
}

inline int morton_magnitude_from_bit_index(int index)
{
  return morton_lod_from_bit_index(index) / 5;
}
inline int morton_magnitude_from_lod(int lod)
{
  return lod / 5;
}
inline int morton_magnitude_to_lod(int magnitude)
{
  return magnitude * 5 + 4;
}

inline int morton_tree_level_to_lod(int magnitude, int level_in_tree)
{
  assert(level_in_tree < 5);
  return morton_magnitude_to_lod(magnitude) - level_in_tree;
}

inline uint8_t morton_get_child_mask(int lod, const morton64_t &morton)
{
  int index = lod * 3;
  if (index < 64)
  {
    uint32_t ret = (morton.data[0] >> index) & 0x7;
    if (index == 63)
      ret |= (morton.data[1] & 0x3) << 1;
    return uint8_t(ret);
  }
  else if (index < 64 * 2)
  {
    index -= 64;
    uint32_t ret = (morton.data[1] >> index) & 0x7;
    if (index == 62)
    {
      ret |= (morton.data[2] & 0x1) << 2;
    }
    return uint8_t(ret);
  }
  index -= 64 * 2;
  return uint8_t((morton.data[2] >> index) & 0x7);
}

inline void morton_set_child_mask(int lod, uint8_t mask, morton64_t &morton)
{
  int index = lod * 3;
  if (index < 64)
  {
    morton.data[0] &= ~(uint64_t(0x7) << index);
    morton.data[0] |= uint64_t(mask) << index;
    if (index == 63)
    {
      morton.data[1] &= ~uint64_t(0x3);
      morton.data[1] |= uint64_t(mask) >> 1;
    }
    return;
  }
  else if (index < 64 * 2)
  {
    index -= 64;
    morton.data[1] &= ~(uint64_t(0x7) << index);
    morton.data[1] |= uint64_t(mask) << index;
    if (index == 62)
    {
      morton.data[2] &= ~uint64_t(0x1);
      morton.data[2] |= uint64_t(mask>>2);
    }
    return;
  }
  index -= 64 * 2;
  morton.data[2] &= ~(uint64_t(0x7) <<  index);
  morton.data[2] |= uint64_t(mask) << index;
}

inline morton64_t morton_mask_create(int lod)
{
  int index = lod * 3 + 3;
  morton64_t a;
  memset(&a, 0, sizeof(a));
  if (index > 63)
    a.data[0] = ~uint64_t(0);
  if (index > 63 * 2)
    a.data[1] = ~uint64_t(0);
  int macro_index = index / 64;
  index = index % 64;
  a.data[macro_index] = (uint64_t(1) << index) - 1;
  return a;
}

inline int morton_lod(const morton64_t &a, const morton64_t &b)
{
  return morton_lod_from_bit_index(morton_msb(morton_xor(a, b)));
}

inline morton64_t morton_mask_create(const morton64_t &a, const morton64_t &b)
{
  return morton_mask_create(morton_lod(a,b));
}

} // namespace morton
} // namespace converter
}



