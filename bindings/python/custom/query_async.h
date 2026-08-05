/* Hand-written bindings for driving queries WITHOUT blocking -- the asyncio path.
 *
 * Dataset.query_box() (custom/query.h) submits a request and blocks until it finishes. That is the
 * right default: most Python callers want one call that returns arrays, and releasing the GIL means
 * other threads keep running. But it parks the calling thread, so it cannot be awaited, and an event
 * loop that calls it stops serving everything else.
 *
 * What is added here is the other half: submit, and let the caller decide how to wait. Three pieces:
 *
 *   Pump.set_wake_callback(fn)  fn fires from a LIBRARY thread when something is ready. It may only
 *                               signal -- from asyncio that means loop.call_soon_threadsafe.
 *   Pump.poll()                 dispatch, on the calling thread. Where completions become visible.
 *   Dataset.query_box_submit()  returns a Request instead of a dict; poll and read .status.
 *
 * Note what is deliberately NOT exposed: a per-request Python completion callback. It would be the
 * obvious design, and it is a lifetime trap -- the C callback outlives the Python Request whenever a
 * caller drops the handle while the request is still running, and releasing a request cannot
 * retroactively unqueue a completion that is already on its way to the dispatch queue. Polling
 * .status after Pump.poll() has none of that: the wake says "look again", and looking is a pure read.
 * examples/python/query_asyncio.py wraps exactly that into a normal `await`.
 *
 * Included by the GENERATED dew_bindings_generated.cpp.
 */
#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <dew/access/query.h>
#include <dew/core/error.h>
#include <dew/core/pump.h>

#include <optional>
#include <string>
#include <vector>

namespace dewpy
{
namespace nb = nanobind;

// Holds the Python wake callable. Lives in PyPump::ctxs, which the generated destructor frees only
// AFTER dew_pump_destroy has returned -- and destroy detaches the waker, which waits out any wake
// already in flight. So the callable cannot be collected while a library thread is inside it.
struct wake_cb_ctx_t : cb_ctx_base
{
  nb::object callback;
};

inline void pump_wake_trampoline(void *user_ptr)
{
  auto *ctx = static_cast<wake_cb_ctx_t *>(user_ptr);
  if (!ctx || !callable_set(ctx->callback))
    return;
  // ARBITRARY library thread: no GIL held, and this is not the interpreter's thread. Acquire before
  // touching anything Python. The callable's own contract is to signal and return -- calling back into
  // dewfall from here (poll included) is not allowed.
  nb::gil_scoped_acquire acquire;
  ctx->callback();
}

template <class ClsT> void bind_pump_async(ClsT &cls)
{
  using Holder = typename ClsT::Type;

  cls.def(
    "set_wake_callback",
    [](Holder &self, nb::object callback) {
      if (!callable_set(callback))
      {
        // Detaching is what waits out an in-flight wake, so it must happen before the old ctx is
        // dropped. Clearing ctxs afterwards releases the Python reference.
        dew_pump_set_wake_callback(self.h, nullptr, nullptr);
        self.ctxs.clear();
        return;
      }
      auto ctx = std::make_unique<wake_cb_ctx_t>();
      ctx->callback = std::move(callback);
      auto *raw = ctx.get();
      self.ctxs.clear(); // replaces any previous callback; detach happens inside the C call below
      self.ctxs.push_back(std::move(ctx));
      dew_pump_set_wake_callback(self.h, &pump_wake_trampoline, raw);
    },
    nb::arg("callback").none(),
    R"doc(Register a callable invoked when there is something for poll() to dispatch.

Fires from an arbitrary library thread, at most once until the next poll(), so a burst of
completions produces a single wake. It may only SIGNAL -- from asyncio, that means
loop.call_soon_threadsafe(...). It must not call back into dewfall, poll() included.

Pass None to detach; this waits for any in-flight callback to return first.)doc");

  cls.def(
    "poll",
    [](Holder &self) { return dew_pump_poll(self.h); },
    R"doc(Dispatch everything that is ready, on the calling thread. Returns the number of events.

The GIL is deliberately held across this call: it dispatches library-internal completions and
returns promptly, and holding it keeps the state a wake callback observes consistent.)doc");

  cls.def(
    "pending_count", [](Holder &self) { return dew_pump_pending_count(self.h); }, R"doc(How many events are waiting for the next poll().)doc");
}

// Result plumbing, shared with query_box (custom/query.h) through dew_dtype_to_numpy_array, so the
// blocking and async paths cannot drift into returning different shapes.
inline nb::dict request_result_dict(dew_request_t *request)
{
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
  return out;
}

inline std::string request_error_message(dew_request_t *request)
{
  dew_error_t *error = nullptr;
  dew_request_get_error(request, &error);
  std::string message = "query did not complete";
  if (error)
  {
    int code = 0;
    const char *text = nullptr;
    size_t length = 0;
    dew_error_get_info(error, &code, &text, &length);
    if (length)
      message.assign(text, length);
    dew_error_destroy(error);
  }
  return message;
}

// Methods on the GENERATED Request holder. It has to be the generated one: the generator emits an
// (empty) Request class for the opaque dew_request_t and registers it after everything else, so a
// second nb::class_ of our own under the same name would simply be overwritten.
template <class ClsT> void bind_request_async(ClsT &cls)
{
  using Holder = typename ClsT::Type;

  cls.def_prop_ro(
    "status", [](Holder &self) { return self.h ? dew_request_status(self.h) : dew_request_canceled; },
    R"doc(The current RequestStatus. Stays `pending` until a Pump.poll() picks the completion up.)doc");

  cls.def_prop_ro(
    "done", [](Holder &self) { return !self.h || dew_request_status(self.h) != dew_request_pending; }, R"doc(True once the status is terminal.)doc");

  cls.def_prop_ro(
    "completion_factor", [](Holder &self) { return self.h ? dew_request_completion_factor(self.h) : 1.0f; }, R"doc(Rough progress in [0, 1]; enough for a progress bar.)doc");

  cls.def(
    "cancel", [](Holder &self) { if (self.h) dew_request_cancel(self.h); },
    R"doc(Ask the request to stop. The terminal status still has to be observed through a poll.)doc");

  cls.def(
    "release",
    [](Holder &self) {
      if (!self.h)
        return;
      dew_request_t *handle = self.h;
      self.h = nullptr;
      // release cancels and waits when the request is still running, so the GIL goes first.
      nb::gil_scoped_release unlock;
      dew_request_release(handle);
    },
    R"doc(Drop the request and its buffers. Idempotent.

Worth doing explicitly: until it is released the dataset keeps the request (and its decoded
points) alive, so a long-lived dataset issuing many queries holds them all. Use the object as a
context manager and it happens for you.)doc");

  cls.def("__enter__", [](nb::object self) { return self; });
  // The three exception arguments are None on a clean exit, so they must be explicitly nullable --
  // nanobind rejects None for an nb::object parameter otherwise, and `with` would raise a TypeError.
  cls.def(
    "__exit__", [](nb::object self, nb::object, nb::object, nb::object) { self.attr("release")(); return false; }, nb::arg("exc_type").none(), nb::arg("exc_value").none(), nb::arg("traceback").none());

  cls.def(
    "result",
    [](Holder &self) {
      if (!self.h)
        throw std::runtime_error("the request has been released");
      dew_request_status_t status = dew_request_status(self.h);
      if (status == dew_request_pending)
        throw std::runtime_error("the request is still pending -- poll the Pump until .done");
      if (status != dew_request_completed)
        throw std::runtime_error(request_error_message(self.h));
      return request_result_dict(self.h);
    },
    R"doc(The result, as the same dict of NumPy arrays that Dataset.query_box() returns.

Raises if the request is still pending, failed, or was cancelled. The arrays are copies, so they
outlive the request.)doc");
}

template <class RequestHolder, class ClsT> void bind_query_submit(ClsT &cls)
{
  using Holder = typename ClsT::Type;

  cls.def(
    "query_box_submit",
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
      // No done callback: see the header comment. Python observes completion by polling .status.
      spec.done = nullptr;
      spec.done_user_ptr = nullptr;

      dew_error_t *error = nullptr;
      dew_request_t *request = dew_dataset_request_region(self.h, &spec, &error);
      if (!request)
      {
        std::string failure;
        if (error)
        {
          int code = 0;
          const char *text = nullptr;
          size_t length = 0;
          dew_error_get_info(error, &code, &text, &length);
          failure.assign(text ? text : "", length);
          dew_error_destroy(error);
        }
        throw std::runtime_error(failure.empty() ? "could not submit the query" : failure);
      }
      // By value, not unique_ptr: the generated holders are movable and nanobind moves this into the
      // Python object. A unique_ptr return would additionally need nanobind/stl/unique_ptr.h.
      RequestHolder out;
      out.h = request;
      return out;
    },
    nb::arg("aabb_min"), nb::arg("aabb_max"), nb::arg("attributes") = nb::none(), nb::arg("lod") = "full", nb::arg("level") = 0, nb::arg("max_points") = 0, nb::arg("clip_points") = true,
    nb::arg("position_format") = "r64",
    R"doc(Submit a box query and return immediately with a Request.

Same arguments as query_box(), but nothing blocks: the request starts on the dataset's own
thread and its status stays `pending` until a Pump.poll() picks the completion up. Drive that
poll from a wake callback (see Pump.set_wake_callback) and you can await the result on an event
loop -- examples/python/query_asyncio.py does exactly that.)doc");
}

} // namespace dewpy
