/* Hand-written binding for the file-convert callback machinery -- the one
 * place the generic generator cannot go:
 *
 *   dew_converter_set_file_converter_callbacks has NO user_ptr, so the
 *   Python callables live in a process-global slot (last set wins; the GIL
 *   serializes all access). Per-file state is fully supported: the C API
 *   threads the void** produced by `init` through convert_data and
 *   destroy_user_ptr, and we store the Python object `init` returns there
 *   together with the destroy callable that was live when the file started.
 *
 *   convert_data exposes the library-allocated output buffers as writable
 *   numpy views shaped (points, columns), matching the library's own stride
 *   formula exactly (size_for_format(type) * components, see
 *   src/converter/input_header.hpp). The views ALIAS library memory and are
 *   only valid during the callback -- Python must fill them, not keep them.
 *   They must be exported with rv_policy::reference: nanobind's default
 *   (automatic_reference) copies when an ndarray has no owner, which would
 *   silently discard everything Python writes.
 *
 * All callbacks fire on the converter's internal threads; every trampoline
 * takes the GIL and lets no exception escape into the C library (which is
 * compiled -fno-exceptions). Python exceptions become the callbacks'
 * dew_error_t** out-param, which the converter reports through its runtime
 * error callback and status.
 *
 * The trampolines are also the memory-safety boundary for Python users:
 * the reader trusts *points_read and the attribute list without validating
 * them (src/converter/reader.cpp), so this layer validates instead of
 * letting a buggy callback corrupt memory.
 *
 * Included by the GENERATED dew_bindings_generated.cpp after the holder
 * structs; the register snippet passes the Converter class and the module.
 */
#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string_view.h>

#include <dew/core/error.h>
#include <dew/converter/converter.h>
#include <dew/converter/laszip_file_convert_callbacks.h>
#include <dew/core/default_attribute_names.h>

#include <cstdint>
#include <string>
#include <utility>

#include "dew_py_support.h"

namespace dewpy
{

struct file_convert_slot
{
  nb::object pre_init, init, convert_data, destroy;
};

inline file_convert_slot &file_slot()
{
  /* Intentionally leaked: the held Python objects must not be destroyed
   * during interpreter finalization from a static destructor. */
  static file_convert_slot *slot = new file_convert_slot;
  return *slot;
}

/* Per-file state threaded through the C API's void**. Carries the destroy
 * callable captured at init time, so re-registering different callbacks does
 * not change how an in-flight file is torn down. */
struct py_file_ctx
{
  nb::object user_obj;
  nb::object destroy;
};

/* What `init` hands to Python. Forwards to the C attribute registrar and
 * counts the registrations, because the reader dereferences attributes[0]
 * unconditionally: an init that registers nothing would be undefined
 * behaviour on an internal thread.
 *
 * `h` points at a stack local of the reader thread (get_data_worker_t::work's
 * tmp_attributes), so it is nulled the moment the callback returns: this
 * object is heap-owned by Python and may outlive the call, and writing through
 * a stale `h` would corrupt the reader's frame. */
struct attrs_proxy
{
  dew_converter_attributes_t *h = nullptr;
  uint32_t count = 0;
  std::string first_name;
  enum dew_type_t first_type = dew_type_u8;
  enum dew_components_t first_components = dew_components_1;
};

/* Nulls attrs_proxy::h on every exit path out of the init trampoline. */
struct attrs_proxy_invalidator
{
  attrs_proxy *p;
  ~attrs_proxy_invalidator()
  {
    if (p)
      p->h = nullptr;
  }
};

inline void set_python_error(dew_error_t **error, const std::string &what)
{
  if (!error)
    return;
  if (!*error)
    *error = dew_error_create();
  if (!*error)
    return;
  dew_error_set_info(*error, -1, what.data(), what.size());
}

/* Mirrors dew::core::size_for_format (src/converter/input_header.hpp).
 * The library's per-point stride is size_for_format(type) * (int)components,
 * with `components` the RAW enum value -- so dew_components_4x4 (== 5) is a
 * stride of five elements, not sixteen. */
inline uint32_t element_size_for(enum dew_type_t type)
{
  switch (type)
  {
  case dew_type_u8:
  case dew_type_i8: return 1;
  case dew_type_u16:
  case dew_type_i16: return 2;
  case dew_type_u32:
  case dew_type_i32:
  case dew_type_r32:
  case dew_type_m32: return 4;
  case dew_type_u64:
  case dew_type_i64:
  case dew_type_r64:
  case dew_type_m64: return 8;
  case dew_type_m128: return 16;
  case dew_type_m192: return 24;
  }
  return 0;
}

/* A writable numpy VIEW over one library-provided convert buffer.
 * Morton types wider than 64 bits are exposed as uint64 lanes, so the
 * column count stays element-exact against the library's stride. */
inline nb::object ndarray_for_buffer(const dew_converter_attribute_t &attr, const dew_converter_buffer_t &buffer,
                                     uint32_t max_points)
{
  nb::dlpack::dtype dtype;
  uint32_t lanes = 1;
  switch (attr.type)
  {
  case dew_type_u8: dtype = nb::dtype<uint8_t>(); break;
  case dew_type_i8: dtype = nb::dtype<int8_t>(); break;
  case dew_type_u16: dtype = nb::dtype<uint16_t>(); break;
  case dew_type_i16: dtype = nb::dtype<int16_t>(); break;
  case dew_type_u32: dtype = nb::dtype<uint32_t>(); break;
  case dew_type_i32: dtype = nb::dtype<int32_t>(); break;
  case dew_type_m32: dtype = nb::dtype<uint32_t>(); break;
  case dew_type_r32: dtype = nb::dtype<float>(); break;
  case dew_type_u64: dtype = nb::dtype<uint64_t>(); break;
  case dew_type_i64: dtype = nb::dtype<int64_t>(); break;
  case dew_type_m64: dtype = nb::dtype<uint64_t>(); break;
  case dew_type_r64: dtype = nb::dtype<double>(); break;
  case dew_type_m128: dtype = nb::dtype<uint64_t>(); lanes = 2; break;
  case dew_type_m192: dtype = nb::dtype<uint64_t>(); lanes = 3; break;
  }
  const uint32_t bytes_per_point = element_size_for(attr.type) * uint32_t(attr.components);
  const uint32_t columns = uint32_t(attr.components) * lanes;
  size_t points = max_points;
  if (bytes_per_point && buffer.size / bytes_per_point < points)
    points = buffer.size / bytes_per_point;
  size_t shape[2] = {points, columns};
  nb::ndarray<nb::numpy> view(buffer.data, 2, shape, nb::handle(), nullptr, dtype);
  /* reference == no copy, no owner: a true view onto the library buffer that
   * only lives for this call. nb::cast's default policy would COPY here and
   * every Python write would be silently discarded. */
  return nb::cast(view, nb::rv_policy::reference);
}

/* How many rows Python was allowed to fill, given the buffers it saw. */
inline uint32_t writable_points(const dew_converter_attribute_t *attributes, uint32_t attributes_size,
                                const dew_converter_buffer_t *buffers, uint32_t buffers_size, uint32_t max_points)
{
  uint32_t limit = max_points;
  const uint32_t n = attributes_size < buffers_size ? attributes_size : buffers_size;
  for (uint32_t i = 0; i < n; ++i)
  {
    const uint32_t bytes_per_point = element_size_for(attributes[i].type) * uint32_t(attributes[i].components);
    if (!bytes_per_point)
      continue;
    const uint32_t fits = uint32_t(buffers[i].size / bytes_per_point);
    if (fits < limit)
      limit = fits;
  }
  return limit;
}

inline dew_converter_file_pre_init_info_t file_pre_init_tramp(const char *filename, size_t filename_size,
                                                             dew_error_t **error)
{
  nb::gil_scoped_acquire gil;
  dew_converter_file_pre_init_info_t info{};
  try
  {
    nb::object ret = file_slot().pre_init(nb::str(filename, filename_size));
    if (!ret.is_none())
      info = nb::cast<dew_converter_file_pre_init_info_t>(ret);
  }
  catch (nb::python_error &e)
  {
    set_python_error(error, e.what());
  }
  catch (const std::exception &e)
  {
    set_python_error(error, e.what());
  }
  catch (...)
  {
    set_python_error(error, "unknown C++ exception in pre_init callback");
  }
  return info;
}

inline void file_init_tramp(const char *filename, size_t filename_size, dew_converter_header_t *header,
                            dew_converter_attributes_t *attributes, void **user_ptr, dew_error_t **error)
{
  nb::gil_scoped_acquire gil;
  /* The reader declares both of these as uninitialized locals and expects the
   * callback to fill them; a partially-filled header would otherwise ingest
   * stack garbage as offset/scale/min/max. */
  *user_ptr = nullptr;
  *header = dew_converter_header_t{};
  try
  {
    /* Both objects handed to Python must be able to outlive this call: the
     * reader's `header` and `attributes` are stack locals of the worker
     * thread, so Python gets its own header instance (copied back below) and
     * a heap-owned registrar that is invalidated on return. */
    nb::object header_obj = nb::cast(dew_converter_header_t{});
    auto *proxy = new attrs_proxy();
    proxy->h = attributes;
    nb::object proxy_obj = nb::cast(proxy, nb::rv_policy::take_ownership);
    attrs_proxy_invalidator invalidator{proxy};

    nb::object ret = file_slot().init(nb::str(filename, filename_size), header_obj, proxy_obj);

    if (proxy->count == 0)
      throw nb::value_error("the init callback registered no attributes; at least "
                            "attributes.add_attribute(dew.ATTRIBUTE_XYZ, ...) is required");
    if (proxy->first_name != DEW_ATTRIBUTE_XYZ)
      throw nb::value_error("the first attribute registered by init must be dew.ATTRIBUTE_XYZ");
    /* sort_points dispatches the morton conversion on the first attribute's
     * format and only implements i32x3 (default: assert(false) on a converter
     * thread). Coordinates are scaled integers: quantize with header.scale. */
    if (proxy->first_type != dew_type_i32 || proxy->first_components != dew_components_3)
      throw nb::value_error("dew.ATTRIBUTE_XYZ must be registered as (dew.Type.i32, "
                            "dew.Components.components_3): coordinates are integers quantized "
                            "by header.scale, not floats");

    /* The header is now deterministically zeroed rather than stack garbage,
     * which turns "forgot to set it" into silent corruption instead of noise:
     * the sorter multiplies every coordinate by scale (zero collapses the whole
     * cloud to one point) and picks the morton width from min/max. Reject the
     * unambiguously broken cases here -- this trampoline is the validation
     * boundary for Python producers. */
    auto filled = nb::cast<dew_converter_header_t>(header_obj);
    for (int k = 0; k < 3; ++k)
    {
      if (filled.scale[k] == 0.0)
        throw nb::value_error("the init callback left header.scale zero; set it to the source's "
                              "coordinate precision (e.g. header.scale = [0.001] * 3)");
      if (filled.min[k] > filled.max[k])
        throw nb::value_error("the init callback set header.min greater than header.max");
    }
    *header = filled;
    *user_ptr = new py_file_ctx{std::move(ret), file_slot().destroy};
  }
  catch (nb::python_error &e)
  {
    set_python_error(error, e.what());
  }
  catch (const std::exception &e)
  {
    set_python_error(error, e.what());
  }
  catch (...)
  {
    set_python_error(error, "unknown C++ exception in init callback");
  }
}

inline void file_convert_data_tramp(void *user_ptr, const dew_converter_header_t *header,
                                    const dew_converter_attribute_t *attributes, uint32_t attributes_size,
                                    uint32_t max_points_to_convert, dew_converter_buffer_t *buffers,
                                    uint32_t buffers_size, uint32_t *points_read, uint8_t *done,
                                    dew_error_t **error)
{
  nb::gil_scoped_acquire gil;
  *points_read = 0;
  *done = 1;
  try
  {
    nb::list buffer_views;
    nb::list attr_list;
    for (uint32_t i = 0; i < buffers_size; ++i)
    {
      const dew_converter_attribute_t &attr = attributes[i < attributes_size ? i : attributes_size - 1];
      buffer_views.append(ndarray_for_buffer(attr, buffers[i], max_points_to_convert));
    }
    for (uint32_t i = 0; i < attributes_size; ++i)
      attr_list.append(nb::make_tuple(nb::str(attributes[i].name, attributes[i].name_size), attributes[i].type,
                                      attributes[i].components));

    auto *ctx = static_cast<py_file_ctx *>(user_ptr);
    nb::object ret = file_slot().convert_data(ctx ? ctx->user_obj : nb::none(), nb::cast(*header), attr_list,
                                              buffer_views, max_points_to_convert);
    auto [read, is_done] = nb::cast<std::pair<uint32_t, bool>>(ret);

    /* The reader resizes every buffer to `read` points WITHOUT reallocating
     * (attribute_buffers_adjust_buffers_to_size), so an oversized count makes
     * the sorter read past the allocations. Refuse instead. */
    const uint32_t writable = writable_points(attributes, attributes_size, buffers, buffers_size, max_points_to_convert);
    if (read > writable)
      throw nb::value_error("convert_data reported more points than the provided buffers hold");

    *points_read = read;
    *done = is_done ? 1 : 0;
  }
  catch (nb::python_error &e)
  {
    set_python_error(error, e.what());
  }
  catch (const std::exception &e)
  {
    set_python_error(error, e.what());
  }
  catch (...)
  {
    set_python_error(error, "unknown C++ exception in convert_data callback");
  }
}

inline void file_destroy_user_ptr_tramp(void *user_ptr)
{
  if (!user_ptr)
    return;
  nb::gil_scoped_acquire gil;
  auto *ctx = static_cast<py_file_ctx *>(user_ptr);
  if (callable_set(ctx->destroy))
  {
    try
    {
      ctx->destroy(ctx->user_obj);
    }
    catch (nb::python_error &e)
    {
      e.discard_as_unraisable("dew file-convert destroy callback");
    }
    catch (...)
    {
      /* nothing to report to: this callback has no error out-param */
    }
  }
  delete ctx;
}

inline void register_file_attributes(nb::module_ &m)
{
  if (nb::hasattr(m, "_FileAttributes"))
    return;
  nb::class_<attrs_proxy>(m, "_FileAttributes",
                          "Attribute registrar handed to a file-convert init callback.")
    .def(
      "add_attribute",
      [](attrs_proxy &self, std::string_view name, enum dew_type_t type, enum dew_components_t components)
      {
        if (!self.h)
          throw nb::value_error("attributes.add_attribute() is only valid while the init callback runs "
                                "(this registrar belongs to a file whose init already returned)");
        dew_converter_attributes_add_attribute(self.h, name.data(), uint32_t(name.size()), type, components);
        if (self.count == 0)
        {
          self.first_name.assign(name);
          self.first_type = type;
          self.first_components = components;
        }
        ++self.count;
      },
      nb::arg("name"), nb::arg("type"), nb::arg("components"),
      "Declare one source attribute. The first one must be dew.ATTRIBUTE_XYZ.")
    .def_prop_ro("count", [](const attrs_proxy &self) { return self.count; });
}

template <class ClsT> void bind_set_file_converter_callbacks(ClsT &cls, nb::module_ &m)
{
  using Holder = typename ClsT::Type;
  register_file_attributes(m);
  cls.def(
    "set_file_converter_callbacks",
    [](Holder &self, nb::object pre_init, nb::object init, nb::object convert_data, nb::object destroy)
    {
      if (!callable_set(pre_init) || !callable_set(init) || !callable_set(convert_data))
        throw nb::value_error("pre_init, init and convert_data are all required "
                              "(the converter invokes each unconditionally)");
      auto &slot = file_slot();
      slot.pre_init = std::move(pre_init);
      slot.init = std::move(init);
      slot.convert_data = std::move(convert_data);
      slot.destroy = std::move(destroy);

      dew_converter_file_convert_callbacks_t callbacks{};
      callbacks.pre_init = &file_pre_init_tramp;
      callbacks.init = &file_init_tramp;
      callbacks.convert_data = &file_convert_data_tramp;
      callbacks.destroy_user_ptr = &file_destroy_user_ptr_tramp;
      dew_converter_set_file_converter_callbacks(self.h, callbacks);
    },
    nb::arg("pre_init").none(), nb::arg("init").none(), nb::arg("convert_data").none(),
    nb::arg("destroy").none() = nb::none(),
    "Feed point data from Python instead of the built-in LAS/LAZ reader.\n\n"
    "pre_init(filename) -> ConverterFilePreInitInfo | None\n"
    "init(filename, header, attributes) -> per-file state object; fill `header`\n"
    "    and register attributes via attributes.add_attribute(...); the first\n"
    "    attribute must be dew.ATTRIBUTE_XYZ\n"
    "convert_data(state, header, attributes, buffers, max_points) ->\n"
    "    (points_written, done); `buffers` are writable numpy views shaped\n"
    "    (max_points, columns) aliasing the converter's own memory, valid only\n"
    "    during the call. Reporting more points than the buffers hold raises.\n"
    "destroy(state) -> None, optional; called when a file is finished\n\n"
    "The callables are stored in one process-global slot (the C API has no\n"
    "registration context): the most recent call wins for every converter in\n"
    "this process. Callbacks run on converter-internal threads.");
}

template <class ClsT> void bind_use_laszip_callbacks(ClsT &cls)
{
  using Holder = typename ClsT::Type;
  cls.def(
    "use_laszip_callbacks",
    [](Holder &self) { dew_converter_set_file_converter_callbacks(self.h, dew_laszip_callbacks()); },
    "Restore the built-in LAS/LAZ file-convert callbacks (the constructor default).");
}

} // namespace dewpy
