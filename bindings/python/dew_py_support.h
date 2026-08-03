/* Hand-written support layer for the generated dewfall Python bindings.
 *
 * Keeps the generator simple: error->exception plumbing, the two-phase
 * out-string helper, and the base class for callback contexts live here.
 * Everything generated includes this header.
 */
#pragma once

#include <nanobind/nanobind.h>

#include <dew/common/error.h>

#include <cstdint>
#include <string>
#include <utility>

namespace nb = nanobind;

namespace dewpy
{

/* Thrown by generated wrappers; translated to the Python dew.Error exception. */
struct error
{
  int code = 0;
  std::string message;
};

/* Creates the dew.Error exception type on the module and registers the
 * translator for dewpy::error. Call first in NB_MODULE. */
void register_error(nb::module_ &m);

/* The dew.Error type object (valid after register_error). */
PyObject *error_type() noexcept;

/* (code, message) from a dew error handle; tolerates null. */
std::pair<int, std::string> error_info(const dew_error_t *err);

/* Convention A (dew_error_t **out): read info, destroy, throw dewpy::error. */
[[noreturn]] void throw_consume(dew_error_t *err, const char *fallback);

/* A dew.Error *instance* for handing a library-owned (borrowed) error to a
 * Python callback. Never destroys err. GIL must be held. */
nb::object error_object(const dew_error_t *err);

/* Convention B (caller-owned dew_error_t *): RAII temp error for one call. */
struct scoped_error
{
  dew_error_t *e;
  scoped_error() : e(dew_error_create()) {}
  ~scoped_error()
  {
    if (e)
      dew_error_destroy(e);
  }
  scoped_error(const scoped_error &) = delete;
  scoped_error &operator=(const scoped_error &) = delete;
  dew_error_t *get() { return e; }
  [[noreturn]] void throw_error(const char *fallback);
};

/* Caller-buffer string read, growing until the result is provably untruncated.
 *
 * The C getters take (buf, capacity) and return the number of characters
 * WRITTEN after truncation -- at most capacity-1, since they always NUL
 * terminate (see attributes_configs_t::attrib_name_registry_get). A return of
 * capacity-1 is therefore indistinguishable from "there was more", so grow and
 * retry until the answer fits strictly inside the buffer. Getters that instead
 * return the required length also converge here, because the retry capacity
 * always exceeds the previous return. */
template <class F> std::string out_string(F &&call)
{
  constexpr size_t k_max_capacity = 1u << 20;
  char stack_buf[256];
  uint32_t written = call(stack_buf, static_cast<uint32_t>(sizeof(stack_buf)));
  if (written + 1u < sizeof(stack_buf))
    return std::string(stack_buf, written);

  for (size_t capacity = sizeof(stack_buf) * 2; capacity <= k_max_capacity; capacity *= 2)
  {
    std::string big(capacity, '\0');
    written = call(big.data(), static_cast<uint32_t>(capacity));
    if (written + 1u < capacity)
    {
      big.resize(written);
      return big;
    }
  }
  std::string big(k_max_capacity, '\0');
  written = call(big.data(), static_cast<uint32_t>(k_max_capacity));
  big.resize(written < k_max_capacity ? written : k_max_capacity - 1);
  return big;
}

/* Owner base for the Python callables captured by callback registrars. A
 * handle holder keeps these alive until after its C destroy call returns
 * (which joins the library's internal threads), so trampolines never see a
 * dangling context. */
struct cb_ctx_base
{
  virtual ~cb_ctx_base() = default;
};

inline bool callable_set(const nb::object &o)
{
  return o.is_valid() && !o.is_none();
}

} // namespace dewpy
