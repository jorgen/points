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

static nb::object make_error_instance(int code, const std::string &message)
{
  nb::object inst = nb::steal(PyObject_CallFunction(s_error_type, "s#", message.c_str(), (Py_ssize_t)message.size()));
  if (!inst.is_valid())
  {
    PyErr_Clear();
    return nb::none();
  }
  PyObject_SetAttrString(inst.ptr(), "code", nb::int_(code).ptr());
  PyObject_SetAttrString(inst.ptr(), "message", nb::str(message.c_str(), message.size()).ptr());
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
