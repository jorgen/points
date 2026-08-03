#include "dew_py_support.h"

namespace dewpy
{

static PyObject *s_error_type = nullptr;

PyObject *error_type() noexcept
{
  return s_error_type;
}

std::pair<int, std::string> error_info(const dew_error_t *err)
{
  if (!err)
    return {0, "unknown dewfall error"};
  int code = 0;
  const char *str = nullptr;
  size_t str_len = 0;
  dew_error_get_info(err, &code, &str, &str_len);
  return {code, str ? std::string(str, str_len) : std::string("unknown dewfall error")};
}

/* Build a dew.Error instance carrying .code and .message.
 *
 * Deliberately avoids the format-string C-API entry points
 * (PyObject_CallFunction(type, "s#", ...) and friends): under Py_LIMITED_API
 * those resolve through the PY_SSIZE_T_CLEAN aliasing and silently fail at
 * runtime on CPython 3.12, so the abi3 wheel lost .code/.message on exactly
 * the oldest interpreter it claims to support while working fine on 3.14. The
 * explicit object-based calls below have unambiguous signatures on every
 * version. Every return value is checked -- a failure here degrades the
 * exception, so it must not pass silently. */
static nb::object make_error_instance(int code, const std::string &message)
{
  nb::object msg = nb::steal(PyUnicode_FromStringAndSize(message.data(), (Py_ssize_t)message.size()));
  if (!msg.is_valid())
  {
    PyErr_Clear();
    return nb::none();
  }
  nb::object inst = nb::steal(PyObject_CallFunctionObjArgs(s_error_type, msg.ptr(), nullptr));
  if (!inst.is_valid())
  {
    PyErr_Clear();
    return nb::none();
  }
  nb::object code_obj = nb::steal(PyLong_FromLong(code));
  if (!code_obj.is_valid() || PyObject_SetAttrString(inst.ptr(), "code", code_obj.ptr()) != 0 ||
      PyObject_SetAttrString(inst.ptr(), "message", msg.ptr()) != 0)
  {
    PyErr_Clear();
    return nb::none();
  }
  return inst;
}

void register_error(nb::module_ &m)
{
  s_error_type = PyErr_NewExceptionWithDoc(
    "dew.Error", "Raised when a dewfall call fails; carries .code and .message.", nullptr, nullptr);
  if (!s_error_type)
    throw nb::python_error();
  m.attr("Error") = nb::borrow<nb::object>(s_error_type);

  nb::register_exception_translator(
    [](const std::exception_ptr &p, void *)
    {
      try
      {
        std::rethrow_exception(p);
      }
      catch (const error &e)
      {
        nb::object inst = make_error_instance(e.code, e.message);
        if (inst.is_none())
          PyErr_SetString(s_error_type, e.message.c_str());
        else
          PyErr_SetObject(s_error_type, inst.ptr());
      }
    });
}

void throw_consume(dew_error_t *err, const char *fallback)
{
  if (!err)
    throw error{-1, fallback};
  auto [code, message] = error_info(err);
  dew_error_destroy(err);
  throw error{code, std::move(message)};
}

nb::object error_object(const dew_error_t *err)
{
  auto [code, message] = error_info(err);
  return make_error_instance(code, message);
}

void scoped_error::throw_error(const char *fallback)
{
  auto [code, message] = error_info(e);
  if (code == 0 && message == "unknown dewfall error")
    throw error{-1, fallback};
  throw error{code, std::move(message)};
}

} // namespace dewpy
