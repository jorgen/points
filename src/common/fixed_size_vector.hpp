/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2023  Jørgen Lind
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

#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <memory>
#include <utility>

namespace points
{
template<typename T>
class fixed_capacity_vector_t
{
public:
  using iterator = T*;
  using const_iterator = const T*;

  template<typename V, typename P>
  fixed_capacity_vector_t(const V &vec, P processor)
    : fixed_capacity_vector_t(std::size(vec))
  {
    for (size_t i = 0; i < _capacity; i++)
    {
      initialize_at(i, processor(vec[i]));
    }
  }

  fixed_capacity_vector_t(std::size_t capacity)
    : _capacity(capacity)
    , _raw_buffer(malloc(_capacity * sizeof(T) + alignof(T) - 1))
  {
    std::size_t aligned_size = _capacity * sizeof(T) + alignof(T) - 1;
    void *p = _raw_buffer;
    _buffer = static_cast<T*>(std::align(alignof(T), sizeof(T), p, aligned_size));
    assert(aligned_size / sizeof(T) == _capacity);
  }

  fixed_capacity_vector_t(fixed_capacity_vector_t&& other)
    : _capacity(other._capacity)
    , _raw_buffer(other._raw_buffer)
    , _buffer(other._buffer)
  {
    other._capacity = 0;
    other._raw_buffer = nullptr;
    other._buffer = nullptr;
  }

  ~fixed_capacity_vector_t() {
    clear();
    free(_raw_buffer);
  }

  fixed_capacity_vector_t& operator=(fixed_capacity_vector_t&& other) noexcept
  {
    if (this != &other) {
      this->~fixed_capacity_vector_t();
      new (this) fixed_capacity_vector_t(std::move(other));
    }
    return *this;
  }

  fixed_capacity_vector_t(const fixed_capacity_vector_t&) = delete;
  fixed_capacity_vector_t& operator=(const fixed_capacity_vector_t&) = delete;

  template<typename... Args>
  void initialize_at(std::size_t index, Args&&... args) {
    assert(index < _capacity);
    new (_buffer + index) T(std::forward<Args>(args)...);
  }

  void clear() {
    for (size_t i = 0; i < _capacity; i++)
    {
      _buffer[i].~T();
    }
    _capacity = 0;
  }


  std::size_t capacity() const { return _capacity; }
  T& operator[](std::size_t index) {
    assert(index < _capacity);
    return _buffer[index];
  }
  const T& operator[](std::size_t index) const {
    assert(index < _capacity);
    return _buffer[index];
  }

  iterator begin() {
      return _buffer;
  }

  const_iterator begin() const {
      return _buffer;
  }

  const_iterator cbegin() const {
      return _buffer;
  }

  iterator end() {
      return _buffer + _capacity;
  }

  const_iterator end() const {
      return _buffer + _capacity;
  }

  const_iterator cend() const {
      return _buffer + _capacity;
  }
private:
  std::size_t _capacity;
  void *_raw_buffer;
  T *_buffer;
};

}
