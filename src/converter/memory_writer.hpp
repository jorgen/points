#pragma once

#include <cstdint>

template <typename T>
bool write_memory(uint8_t *&ptr, const uint8_t *end, const T &value)
{
  if (ptr + sizeof(value) > end)
    return false;
  memcpy(ptr, &value, sizeof(value));
  ptr += sizeof(value);
  return true;
}

template <typename T>
bool write_vec_type(uint8_t *&ptr, const uint8_t *end, const T &data)
{
  auto to_copy = sizeof(data[0]) * data.size();
  if (ptr + to_copy > end)
    return false;
  memcpy(ptr, &data[0], to_copy);
  ptr += to_copy;
  return true;
}

template <typename T>
bool read_memory(const uint8_t *&ptr, const uint8_t *end, T &value)
{
  if (ptr + sizeof(value) > end)
    return false;
  memcpy(&value, ptr, sizeof(value));
  ptr += sizeof(value);
  return true;
}

template <typename T>
bool read_vec_type(const uint8_t *&ptr, const uint8_t *end, T &data, uint32_t size)
{
  data.resize(size);
  auto to_copy = sizeof(data[0]) * size;
  if (ptr + to_copy > end)
    return false;
  memcpy(&data[0], ptr, to_copy);
  ptr += to_copy;
  return true;
}