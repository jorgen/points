/* Hand-written binding for the box query.
 *
 * The C request lifecycle -- submit, poll or wait, borrow buffers, release -- is the right shape for
 * C and the wrong shape for Python. A Python caller wants one call that returns arrays. So
 * Dataset.query_box() runs the whole request and copies the results into NumPy arrays that Python
 * owns, then releases the request.
 *
 * The copy is deliberate. dew_request_get_result hands back pointers into request-owned memory that
 * dies at dew_request_release, so a zero-copy view would dangle the moment this function returned.
 * (This is the mirror image of the trap in file_convert_callbacks.h, where an ndarray MUST alias
 * library memory and must therefore be exported with rv_policy::reference; here we want the copy
 * that nanobind would otherwise make by accident.)
 *
 * Included by the GENERATED dew_bindings_generated.cpp; the register snippet passes the Dataset
 * class.
 */
#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <dew/access/query.h>
#include <dew/core/error.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace dewpy
{
namespace nb = nanobind;

inline nb::object dew_dtype_to_numpy_array(const dew_attribute_buffer_t &buffer, uint64_t point_count)
{
  const uint32_t components = uint32_t(buffer.components);
  // Copy into Python-owned storage: the source dies with the request.
  auto make = [&](auto sample) {
    using T = decltype(sample);
    const size_t n = size_t(point_count) * components;
    auto *owned = new T[n];
    if (buffer.data && buffer.size_bytes >= n * sizeof(T))
      memcpy(owned, buffer.data, n * sizeof(T));
    else
      memset(owned, 0, n * sizeof(T));
    nb::capsule deleter(owned, [](void *p) noexcept { delete[] static_cast<T *>(p); });
    size_t shape[2] = {size_t(point_count), size_t(components)};
    if (components == 1)
      return nb::cast(nb::ndarray<nb::numpy, T, nb::ndim<1>>(owned, 1, shape, deleter));
    return nb::cast(nb::ndarray<nb::numpy, T, nb::ndim<2>>(owned, 2, shape, deleter));
  };

  switch (buffer.type)
  {
  case dew_type_u8:
    return make(uint8_t{});
  case dew_type_i8:
    return make(int8_t{});
  case dew_type_u16:
    return make(uint16_t{});
  case dew_type_i16:
    return make(int16_t{});
  case dew_type_u32:
    return make(uint32_t{});
  case dew_type_i32:
    return make(int32_t{});
  case dew_type_r32:
    return make(float{});
  case dew_type_u64:
    return make(uint64_t{});
  case dew_type_i64:
    return make(int64_t{});
  case dew_type_r64:
    return make(double{});
  default:
    return nb::none();
  }
}

template <class ClsT> void bind_query_box(ClsT &cls)
{
  using Holder = typename ClsT::Type;
  cls.def(
    "query_box",
    [](Holder &self, std::vector<double> aabb_min, std::vector<double> aabb_max, std::optional<std::vector<std::string>> attributes, const std::string &lod, int32_t level, uint64_t max_points,
       bool clip_points, const std::string &position_format) {
      if (aabb_min.size() != 3 || aabb_max.size() != 3)
        throw nb::value_error("aabb_min and aabb_max must each have 3 elements");

      std::vector<std::string> names = attributes.value_or(std::vector<std::string>{});
      std::vector<const char *> name_ptrs;
      name_ptrs.reserve(names.size());
      for (auto &n : names)
        name_ptrs.push_back(n.c_str());

      dew_region_request_t spec{};
      for (int i = 0; i < 3; i++)
      {
        spec.aabb_min[i] = aabb_min[size_t(i)];
        spec.aabb_max[i] = aabb_max[size_t(i)];
      }
      if (lod == "full")
        spec.lod_mode = dew_lod_full;
      else if (lod == "level")
        spec.lod_mode = dew_lod_level;
      else if (lod == "budget")
        spec.lod_mode = dew_lod_point_budget;
      else
        throw nb::value_error("lod must be one of 'full', 'level', 'budget'");
      spec.lod = level;
      spec.max_points = max_points;
      spec.attribute_names = name_ptrs.empty() ? nullptr : name_ptrs.data();
      spec.attribute_count = uint32_t(name_ptrs.size());
      if (position_format == "r64")
        spec.position_format = dew_position_r64_absolute;
      else if (position_format == "r32")
        spec.position_format = dew_position_r32_relative;
      else if (position_format == "i32")
        spec.position_format = dew_position_i32_grid;
      else
        throw nb::value_error("position_format must be one of 'r64', 'r32', 'i32'");
      spec.clip_mode = clip_points ? dew_clip_point : dew_clip_node;

      dew_request_t *request = nullptr;
      dew_request_status_t status = dew_request_failed;
      std::string failure;
      {
        // The query blocks on storage; let other Python threads run meanwhile.
        nb::gil_scoped_release release;
        dew_error_t *error = nullptr;
        request = dew_dataset_request_region(self.h, &spec, &error);
        if (!request)
        {
          if (error)
          {
            int code = 0;
            const char *msg = nullptr;
            size_t len = 0;
            dew_error_get_info(error, &code, &msg, &len);
            failure.assign(msg ? msg : "", len);
            dew_error_destroy(error);
          }
        }
        else
        {
          status = dew_request_wait(request, -1);
        }
      }
      if (!request)
        throw std::runtime_error(failure.empty() ? "query failed" : failure);

      if (status != dew_request_completed)
      {
        dew_error_t *error = nullptr;
        dew_request_get_error(request, &error);
        std::string message = "query did not complete";
        if (error)
        {
          int code = 0;
          const char *msg = nullptr;
          size_t len = 0;
          dew_error_get_info(error, &code, &msg, &len);
          if (len)
            message.assign(msg, len);
          dew_error_destroy(error);
        }
        dew_request_release(request);
        throw std::runtime_error(message);
      }

      dew_request_result_t result{};
      nb::dict out;
      if (dew_request_get_result(request, &result))
      {
        for (uint32_t i = 0; i < result.buffer_count; i++)
        {
          const auto &buffer = result.buffers[i];
          std::string key = (i == 0) ? std::string("xyz") : std::string(buffer.name, buffer.name_size);
          if (key.empty())
            key = "attribute_" + std::to_string(i);
          out[key.c_str()] = dew_dtype_to_numpy_array(buffer, result.point_count);
        }
        out["point_count"] = result.point_count;
        out["node_count"] = result.node_count;
      }
      dew_request_release(request);
      return out;
    },
    nb::arg("aabb_min"), nb::arg("aabb_max"), nb::arg("attributes") = nb::none(), nb::arg("lod") = "full", nb::arg("level") = 0, nb::arg("max_points") = 0, nb::arg("clip_points") = true,
    nb::arg("position_format") = "r64",
    R"doc(Query the points inside an axis-aligned box.

Returns a dict of NumPy arrays: 'xyz' with shape (N, 3) plus one entry per requested
attribute, along with 'point_count' and 'node_count'.

aabb_min / aabb_max  the box, in world coordinates (3 elements each)
attributes           attribute names to fetch alongside the positions, e.g. ["intensity"]
lod                  'full' for every source point, 'level' to stop at `level`,
                     'budget' to descend while under `max_points`
clip_points          True returns exactly the points inside the box; False returns whole
                     octree nodes that overlap it, which is faster but overshoots
position_format      'r64' absolute doubles (lossless), 'r32' or 'i32' relative to each node

The arrays are copies that Python owns; the underlying request is released before returning.
)doc");
}

} // namespace dewpy
