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

template<typename T, size_t C>
struct morton_t
{
  using component_type = T;
  using decomposed_type = T;
  using component_count = std::integral_constant<size_t, C>;
  static_assert(C > 0 && C < 4, "invalid number of components for  morton_t");
  T data[C];
};
template<>
struct morton_t<uint64_t, 1>
{
  using component_type = uint64_t;
  using decomposed_type = uint32_t;
  using component_count = std::integral_constant<size_t, 1>;
  uint64_t data[1];
};

using morton32_t = morton_t<uint32_t, 1>;
using morton64_t = morton_t<uint64_t, 1>;
using morton128_t = morton_t<uint64_t, 2>;
using morton192_t = morton_t<uint64_t, 3>;

template<typename T, size_t C>
inline bool operator==(const morton_t<T, C> &a, const morton_t<T, C> &b)
{
  return memcmp(&a, &b, sizeof(a)) == 0;
}

template<typename T, size_t C>
inline bool operator<(const morton_t<T, C> &a, const morton_t<T,C> &b)
{
  if (C > 2)
  {
    if (a.data[2] != b.data[2])
      return a.data[2] < b.data[2];
  }
  if (C > 1)
  {
    if (a.data[1] != b.data[1])
      return a.data[1] < b.data[1];
  }
  return a.data[0] < b.data[0];
}

template<typename T, size_t C>
inline bool operator>(const morton_t<T, C> &a, const morton_t<T,C> &b)
{
  if (C > 2)
  {
    if (a.data[2] != b.data[2])
      return a.data[2] > b.data[2];
  }
  if (C > 1)
  {
    if (a.data[1] != b.data[1])
      return a.data[1] > b.data[1];
  }
  return a.data[0] > b.data[0];
}

template<typename T, size_t C>
void morton_init_min(morton_t<T,C> &a)
{
  a.data[0] = 0;
  if (C > 1)
    a.data[1] = 0;
  if (C > 2)
    a.data[2] = 0;
}
template<typename T, size_t C>
void morton_init_max(morton_t<T,C> &a)
{
  a.data[0] = ~T(0);
  if (C > 1)
    a.data[1] = ~T(0);
  if (C > 2)
    a.data[2] = ~T(0);
}

template<typename T, size_t C>
inline morton_t<T,C> morton_xor(const morton_t<T,C> &a, const morton_t<T,C> &b)
{
  morton_t<T,C> c;
  c.data[0] = a.data[0] ^ b.data[0];
  if (C > 1)
    c.data[1] = a.data[1] ^ b.data[1];
  if (C > 2)
    c.data[2] = a.data[2] ^ b.data[2];
  return c;
}

template<typename T, size_t C>
inline bool morton_is_null(const morton_t<T,C> &a)
{
  return a.data[0] == T(0) && C > 1 ? a.data[1] == T(0) : true && C > 2 ? a.data[2] == T(0) : true;
}

template<typename T, size_t C>
inline morton_t<T,C> morton_or(const morton_t<T,C> &a, const morton_t<T,C> &b)
{
  morton_t<T,C> c;
  c.data[0] = a.data[0] | b.data[0];
  if (C > 1)
    c.data[1] = a.data[1] | b.data[1];
  if (C > 2)
    c.data[2] = a.data[2] | b.data[2];
  return c;
}

template<typename T, size_t C>
inline morton_t<T,C> morton_negate(const morton_t<T,C> &a)
{
  morton_t<T,C> b;
  b.data[0] = ~a.data[0];
  if (C > 1)
    b.data[1] = ~a.data[1];
  if (C > 2)
    b.data[2] = ~a.data[2];
  return b;
}

template<typename T, size_t C>
inline morton_t<T,C> morton_and(const morton_t<T,C> &a, const morton_t<T,C> &b)
{
  morton_t<T,C> c;
  c.data[0] = a.data[0] & b.data[0];
  if (C > 1)
    c.data[1] = a.data[1] & b.data[1];
  if (C > 2)
    c.data[2] = a.data[2] & b.data[2];
  return c;
}

template<typename T, size_t C>
inline void morton_add_one(morton_t<T,C> &a)
{
  if (a.data[0] == (~T(0)))
  {
    a.data[0] = 0;
    if (C > 1)
    {
      if (a.data[1] == (~T(0)))
      {
        a.data[1] = 0;
        if (C > 2)
          a.data[2]++;
      }
      else
      {
        a.data[1]++;
      }
    }
  }
  else
  {
    a.data[0]++;
  }
}

template<typename T, size_t C>
inline morton_t<T, C> morton_add(const morton_t<T, C> &a, const morton_t<T, C> &b)
{
  morton_t<T, C> ret;
  ret.data[0] = a.data[0] + b.data[0];
  if (C > 1)
  {
    int carry = ret.data[0] < a.data[0]? 1 : 0;
    ret.data[1] = a.data[1] + b.data[1] + carry;
    if (C > 2)
    {
      carry = ret.data[1] < a.data[1]? 1 : 0;
      ret.data[2] = a.data[2] + b.data[2] + carry;

    }
  }
  return ret;
}

template<typename T, size_t C>
inline bool morton_is_set(const morton_t<T, C> &a)
{
  return a.data[0] || C > 1 ? a.data[1] : false || C > 2 ? a.data[2] : false;
}

inline int bit_scan_reverse(uint32_t a)
{
#ifdef _MSC_VER
  _BitScanReverse(&index, a);
  return int(index);
#else
  static_assert(sizeof(unsigned) == sizeof(uint32_t), "Wrong size for builtin_clzll");
  return 31 - __builtin_clz(a);
#endif
}

inline int bit_scan_reverse(uint64_t a)
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

template<typename T, size_t C>
inline int morton_msb(const morton_t<T, C> &a)
{
  if (C > 2)
  {
    if (a.data[2])
    {
      return 64 * 2 + bit_scan_reverse(a.data[2]);
    }
  }
  if (C > 1)
  {
    if (a.data[1])
    {
      return 64 * 1 + bit_scan_reverse(a.data[1]);
    }
  }
  if (a.data[0])
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

template<typename T, size_t C>
inline uint8_t morton_get_child_mask(int lod, const morton_t<T,C> &morton)
{
  int index = lod * 3;
  assert(index < int(sizeof(T) * 8 * C));
  if (index < int(sizeof(T) * 8))
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

template<typename T, size_t C>
inline void morton_set_child_mask(int lod, uint8_t mask, morton_t<T,C> &morton)
{
  assert(mask < 8);
  int index = lod * 3;
  assert(index < int(sizeof(T) * 8 * C));
  if (index < int(sizeof(T) * 8))
  {
    morton.data[0] &= ~(T(0x7) << index);
    morton.data[0] |= T(mask) << index;
    if (C > 1 && index == 63)
    {
      morton.data[1] &= ~uint64_t(0x3);
      morton.data[1] |= uint64_t(mask) >> 1;
    }
    return;
  }
  else if (C > 1 && index < 64 * 2)
  {
    index -= 64;
    morton.data[1] &= ~(uint64_t(0x7) << index);
    morton.data[1] |= uint64_t(mask) << index;
    if (C > 2 && index == 62)
    {
      morton.data[2] &= ~uint64_t(0x1);
      morton.data[2] |= uint64_t(mask>>2);
    }
    return;
  }
  if (C > 2)
  {
    index -= 64 * 2;
    morton.data[2] &= ~(uint64_t(0x7) <<  index);
    morton.data[2] |= uint64_t(mask) << index;
  }
}

template<typename T, size_t C>
inline morton_t<T,C> morton_mask_create(int lod)
{
  static_assert(C > 0, "invalid size");
  static_assert(std::is_same<T, uint32_t>::value ? C == 1 : true, "Only support one component 32 morton");

  int index = lod * 3 + 3;
  assert(index < int(sizeof(T) * 8 * C));

  morton_t<T,C> a;
  memset(&a, 0, sizeof(a));
  int macro_index =  0;
  if (C > 1 && index > 63)
  {
    a.data[0] = ~T(0);
    if (C > 2 && index > 63 * 2)
    {
      a.data[1] = ~T(0);
      macro_index = 2;
    }
    else
    {
      macro_index = 1;
    }
    index = index % 64;
  }
  a.data[macro_index] = (T(1) << index) - 1;
  return a;
}

template<typename T, size_t C>
inline int morton_lod(const morton_t<T,C> &a, const morton_t<T,C> &b)
{
  return morton_lod_from_bit_index(morton_msb(morton_xor(a, b)));
}

template<typename T, size_t C>
inline morton_t<T,C> morton_mask_create(const morton_t<T,C> &a, const morton_t<T,C> &b)
{
  return morton_mask_create<T,C>(morton_lod(a,b));
}

template<typename T1, size_t C1, typename T2, size_t C2>
inline void morton_downcast(const morton_t<T1, C1> &a, morton_t<T2, C2> &b)
{
  static_assert(sizeof(T1) >= sizeof(T2), "invalid size");
  static_assert(C1 >= C2, "invalid size");
  static_assert(C1 > 0 && C2 > 0, "invalid size");
  static_assert(std::is_same<T2, uint32_t>::value ? C2 == 1 : true, "Only support one component 32 morton");
  b.data[0] = T2(a.data[0]);
  if (C2 > 1)
  {
    b.data[1] = a.data[1];
  }
  if (C2 > 2)
  {
    b.data[2] = a.data[2];
  }
}

template<typename T1, size_t C1, typename T2, size_t C2>
inline void morton_upcast(const morton_t<T1, C1> &a, const morton_t<T2, C2> &min, morton_t<T2, C2> &b)
{
  static_assert(sizeof(T1) <= sizeof(T2), "invalid size");
  static_assert(C1 <= C2, "invalid size");
  static_assert(C1 > 0 && C2 > 0, "invalid size");
  static_assert(std::is_same<T1, uint32_t>::value ? C1 == 1 : true, "Only support one component 32 morton");
  b.data[0] = a.data[0];
  if (std::is_same<T1, uint32_t>::value && std::is_same<T2, uint64_t>::value)
  {
    b.data[0] |= (min.data[0] & (~T2(0) << (sizeof(T2) * 4)));
  }
  if (C1 > 1)
  {
    b.data[1] = a.data[1];
  }
  else
  {
    b.data[1] = min.data[1];
  }
  if (C1 > 2)
  {
    b.data[2] = a.data[2];
  }
  else
  {
    b.data[2] = min.data[2];
  }
}

inline void decode(const morton192_t &morton, uint64_t (&decoded)[3])
{
  uint32_t lower[3];
  uint32_t mid[3];
  uint32_t high[3];

  libmorton::morton3D_64_decode(morton.data[0], lower[0], lower[1], lower[2]);
  libmorton::morton3D_64_decode(morton.data[1], mid[0], mid[1], mid[2]);
  libmorton::morton3D_64_decode(morton.data[2], high[0], high[1], high[2]);

  decoded[0] = uint64_t(lower[0]) | uint64_t(mid[0]) << 22 | uint64_t(high[0]) << (22 + 21);
  decoded[1] = uint64_t(lower[1]) | uint64_t(mid[1]) << 21 | uint64_t(high[1]) << (21 + 22);
  decoded[2] = uint64_t(lower[2]) | uint64_t(mid[2]) << 21 | uint64_t(high[1]) << (21 + 21);
}

inline void decode(const morton128_t &morton, uint64_t (&decoded)[3])
{
  uint32_t lower[3];
  uint32_t mid[3];

  libmorton::morton3D_64_decode(morton.data[0], lower[0], lower[1], lower[2]);
  libmorton::morton3D_64_decode(morton.data[1], mid[0], mid[1], mid[2]);

  decoded[0] = uint64_t(lower[0]) | uint64_t(mid[0]) << 22;
  decoded[1] = uint64_t(lower[1]) | uint64_t(mid[1]) << 21;
  decoded[2] = uint64_t(lower[2]) | uint64_t(mid[2]) << 21;
}

inline void decode(const morton_t<uint64_t, 1> &morton, uint64_t (&decoded)[3])
{
  uint_fast32_t tmp[3];
  libmorton::morton3D_64_decode(morton.data[0], tmp[0], tmp[1], tmp[2]);
  decoded[0] = tmp[0];
  decoded[1] = tmp[1];
  decoded[2] = tmp[2];
}

inline void decode(const morton32_t &morton, uint64_t (&decoded)[3])
{
  uint_fast16_t tmp[3];
  libmorton::morton3D_32_decode(morton.data[0], tmp[0], tmp[1], tmp[2]);
  decoded[0] = tmp[0];
  decoded[1] = tmp[1];
  decoded[2] = tmp[2];
}

inline void encode(const uint64_t (&pos)[3], morton192_t &morton)
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

inline void encode(const uint64_t (&pos)[3], morton128_t &morton)
{
  constexpr uint32_t mask21 = (uint32_t(1) << 21) - 1;
  constexpr uint32_t mask22 = (uint32_t(1) << 22) - 1;

  uint32_t x_lower = pos[0] & mask22;
  uint32_t x_mid = (pos[0] >> 22) & mask21;

  uint32_t y_lower = pos[1] & mask21;
  uint32_t y_mid = (pos[1] >> 21) & mask22;

  uint32_t z_lower = pos[2] & mask21;
  uint32_t z_mid = (pos[2] >> 21) & mask21;

  // X starts at LSB
  morton.data[0] = libmorton::morton3D_64_encode(x_lower, y_lower, z_lower);
  morton.data[1] = libmorton::morton3D_64_encode(x_mid, y_mid, z_mid);
}

inline void encode(const uint64_t (&pos)[3], morton_t<uint64_t,1> &morton)
{
  constexpr uint32_t mask21 = (uint32_t(1) << 21) - 1;
  constexpr uint32_t mask22 = (uint32_t(1) << 22) - 1;

  uint32_t x_lower = pos[0] & mask22;

  uint32_t y_lower = pos[1] & mask21;

  uint32_t z_lower = pos[2] & mask21;

  // X starts at LSB
  morton.data[0] = libmorton::morton3D_64_encode(x_lower, y_lower, z_lower);
}

inline void encode(const uint64_t (&pos)[3], morton32_t &morton)
{
  constexpr uint32_t mask21 = (uint32_t(1) << 21) - 1;
  constexpr uint32_t mask22 = (uint32_t(1) << 22) - 1;

  uint_fast16_t x_lower = uint_fast16_t(pos[0] & mask22);

  uint_fast16_t y_lower = uint_fast16_t(pos[1] & mask21);

  uint_fast16_t z_lower = uint_fast16_t(pos[2] & mask21);

  // X starts at LSB
  morton.data[0] = libmorton::morton3D_32_encode(x_lower, y_lower, z_lower);
}




} // namespace morton
} // namespace converter
}



