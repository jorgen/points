/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#include "http_io_manager.hpp"

#include <ctime>
#include <cstdio>
#include <cstring>

namespace points::converter
{

http_io_manager_t::http_io_manager_t(vio::event_loop_t &event_loop)
  : _event_loop(event_loop)
{
}

std::string http_io_manager_t::host_header(bool https, const std::string &host, uint16_t port)
{
  uint16_t def = https ? 443 : 80;
  if (port == 0 || port == def)
    return host;
  return host + ":" + std::to_string(port);
}

void http_io_manager_t::utc_now(std::string &amz_date, std::string &date_stamp, std::string &rfc1123_date)
{
  static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  time_t t = time(nullptr);
  struct tm g;
#ifdef _WIN32
  gmtime_s(&g, &t);
#else
  gmtime_r(&t, &g);
#endif
  char buf[64];
  // yyyyMMddTHHmmssZ and yyyyMMdd are numeric -> locale-independent.
  snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
  amz_date = buf;
  snprintf(buf, sizeof(buf), "%04d%02d%02d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);
  date_stamp = buf;
  // RFC 1123 with fixed English names (HTTP requires them regardless of locale).
  snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT", days[g.tm_wday], g.tm_mday, months[g.tm_mon], g.tm_year + 1900, g.tm_hour, g.tm_min, g.tm_sec);
  rfc1123_date = buf;
}

static points_error_t http_error(int status, const std::string &op, const std::string &body)
{
  points_error_t e;
  e.code = -1;
  std::string snippet = body.substr(0, 400);
  e.msg = op + " failed: HTTP " + std::to_string(status) + (snippet.empty() ? "" : (" " + snippet));
  return e;
}

vio::task_t<points_error_t> http_io_manager_t::do_request(vio::http::request_t req, vio::http::response_t &out) const
{
  auto resp = co_await vio::http::fetch(_event_loop, req);
  if (!resp.has_value())
  {
    points_error_t e;
    e.code = resp.error().code ? resp.error().code : -1;
    e.msg = "http request failed: " + resp.error().msg;
    co_return e;
  }
  out = std::move(resp.value());
  co_return points_error_t{};
}

vio::task_t<points_error_t> http_io_manager_t::read_object(std::string name, uint8_t *dst, io_range_t range, uint32_t &bytes_read)
{
  auto req = build_request("GET", name, std::span<const uint8_t>{}, &range);
  vio::http::response_t resp;
  auto err = co_await do_request(std::move(req), resp);
  if (err.code != 0)
    co_return err;
  if (resp.status != 200 && resp.status != 206)
    co_return http_error(resp.status, "read_object " + name, resp.body);

  uint64_t want = range.size >= 0 ? uint64_t(range.size) : resp.body.size();
  uint64_t n = resp.body.size() < want ? resp.body.size() : want;
  if (n > 0)
    memcpy(dst, resp.body.data(), n);
  bytes_read = uint32_t(n);
  co_return points_error_t{};
}

vio::task_t<points_error_t> http_io_manager_t::write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size)
{
  std::span<const uint8_t> payload(data.get(), size);
  auto req = build_request("PUT", name, payload, nullptr);
  req.body.assign(reinterpret_cast<const char *>(data.get()), size);
  vio::http::response_t resp;
  auto err = co_await do_request(std::move(req), resp);
  if (err.code != 0)
    co_return err;
  if (resp.status != 200 && resp.status != 201)
    co_return http_error(resp.status, "write_object " + name, resp.body);
  co_return points_error_t{};
}

vio::task_t<points_error_t> http_io_manager_t::object_info(std::string name, object_info_t &out)
{
  auto req = build_request("HEAD", name, std::span<const uint8_t>{}, nullptr);
  vio::http::response_t resp;
  auto err = co_await do_request(std::move(req), resp);
  if (err.code != 0)
    co_return err;
  if (resp.status == 404)
  {
    out.exists = false;
    out.size = 0;
    co_return points_error_t{};
  }
  if (resp.status != 200)
    co_return http_error(resp.status, "object_info " + name, resp.body);
  out.exists = true;
  out.size = 0;
  std::string cl(resp.header("content-length"));
  if (!cl.empty())
    out.size = std::strtoull(cl.c_str(), nullptr, 10);
  co_return points_error_t{};
}

vio::task_t<points_error_t> http_io_manager_t::remove_object(std::string name)
{
  auto req = build_request("DELETE", name, std::span<const uint8_t>{}, nullptr);
  vio::http::response_t resp;
  auto err = co_await do_request(std::move(req), resp);
  if (err.code != 0)
    co_return err;
  // Idempotent: a missing object (404/410) is success, as are 200/202/204.
  if (resp.status == 200 || resp.status == 202 || resp.status == 204 || resp.status == 404 || resp.status == 410)
    co_return points_error_t{};
  co_return http_error(resp.status, "remove_object " + name, resp.body);
}

} // namespace points::converter
