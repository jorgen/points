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
#include "s3_io_manager.hpp"

#include "cloud_signing.hpp"

#include <vio/crypto.h>

#include <span>

namespace points::converter
{

s3_io_manager_t::s3_io_manager_t(vio::event_loop_t &event_loop, config_t cfg)
  : http_io_manager_t(event_loop)
  , _cfg(std::move(cfg))
{
  _allow_plaintext = !_cfg.https;
}

std::string s3_io_manager_t::key_for(const std::string &name) const
{
  if (_cfg.prefix.empty())
    return name;
  return _cfg.prefix + "/" + name;
}

vio::http::request_t s3_io_manager_t::build_request(const std::string &method, const std::string &name, std::span<const uint8_t> payload, const io_range_t *range) const
{
  using cloud::signed_header_t;
  using cloud::uri_encode;

  std::string amz_date, date_stamp, rfc1123;
  utc_now(amz_date, date_stamp, rfc1123);

  std::string key = key_for(name);

  std::string canonical_uri;
  std::string url_host; // host[:port]? no — host without port; port appended to URL below
  if (_cfg.path_style)
  {
    canonical_uri = "/" + uri_encode(_cfg.bucket, false) + "/" + uri_encode(key, true);
    url_host = _cfg.host;
  }
  else
  {
    canonical_uri = "/" + uri_encode(key, true);
    url_host = _cfg.bucket + "." + _cfg.host;
  }

  std::string host_value = host_header(_cfg.https, url_host, _cfg.port);

  std::string url = (_cfg.https ? "https://" : "http://") + host_value + canonical_uri;

  std::string payload_sha = vio::crypto::to_hex(vio::crypto::sha256(payload));

  std::vector<signed_header_t> signed_headers = {{"host", host_value}, {"x-amz-content-sha256", payload_sha}, {"x-amz-date", amz_date}};
  std::string authorization = cloud::aws_sigv4_authorization(method, canonical_uri, "", signed_headers, payload_sha, _cfg.access_key, _cfg.secret_key, _cfg.region, "s3", amz_date, date_stamp);

  vio::http::request_t req;
  req.method = method;
  req.url = std::move(url);
  req.allow_plaintext = _allow_plaintext;
  req.ca_mem = _ca_mem;
  // Host and Content-Length are added by vio::http::fetch; do not duplicate them here.
  req.headers.push_back({"x-amz-date", amz_date});
  req.headers.push_back({"x-amz-content-sha256", payload_sha});
  req.headers.push_back({"Authorization", authorization});
  if (range && range->size >= 0 && method == "GET")
  {
    int64_t start = range->offset;
    int64_t end = range->offset + range->size - 1;
    req.headers.push_back({"Range", "bytes=" + std::to_string(start) + "-" + std::to_string(end)});
  }
  return req;
}

} // namespace points::converter
