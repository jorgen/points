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
#include "azure_io_manager.hpp"

#include "cloud_signing.hpp"

#include <span>

namespace points::converter
{

azure_io_manager_t::azure_io_manager_t(vio::event_loop_t &event_loop, config_t cfg)
  : http_io_manager_t(event_loop)
  , _cfg(std::move(cfg))
{
  _allow_plaintext = !_cfg.https;
}

std::string azure_io_manager_t::blob_for(const std::string &name) const
{
  if (_cfg.prefix.empty())
    return name;
  return _cfg.prefix + "/" + name;
}

vio::http::request_t azure_io_manager_t::build_request(const std::string &method, const std::string &name, std::span<const uint8_t> payload, const io_range_t *range) const
{
  using cloud::signed_header_t;
  using cloud::uri_encode;

  std::string amz_date, date_stamp, rfc1123;
  utc_now(amz_date, date_stamp, rfc1123);

  std::string blob = blob_for(name);
  // The request URI path. Azurite carries the account in the path; real Azure carries it in the host.
  std::string path = _cfg.account_in_path ? ("/" + _cfg.account + "/" + _cfg.container + "/" + blob) : ("/" + _cfg.container + "/" + blob);

  std::string host_value = host_header(_cfg.https, _cfg.host, _cfg.port);
  std::string url = (_cfg.https ? "https://" : "http://") + host_value + uri_encode(path, true);

  std::string range_str;
  if (range && range->size >= 0 && method == "GET")
    range_str = "bytes=" + std::to_string(range->offset) + "-" + std::to_string(range->offset + range->size - 1);

  const bool is_put = (method == "PUT");

  std::vector<signed_header_t> x_ms;
  x_ms.push_back({"x-ms-date", rfc1123});
  x_ms.push_back({"x-ms-version", _cfg.api_version});
  if (is_put)
    x_ms.push_back({"x-ms-blob-type", "BlockBlob"});

  vio::http::request_t req;
  req.method = method;
  req.allow_plaintext = _allow_plaintext;
  req.ca_mem = _ca_mem;
  for (const auto &h : x_ms)
    req.headers.push_back({h.name, h.value});

  if (!_cfg.sas.empty())
  {
    // SAS token carries the authorization in the query string; no signing needed.
    url += (url.find('?') == std::string::npos ? "?" : "&") + _cfg.sas;
  }
  else
  {
    std::string content_length = (is_put && payload.size() > 0) ? std::to_string(payload.size()) : std::string();
    // Canonicalized resource is "/{account}{uri-path}"; for Azurite the account legitimately repeats.
    std::string canonical_resource = "/" + _cfg.account + path;
    std::string authorization = cloud::azure_sharedkey_authorization(method, _cfg.account, _cfg.account_key_base64, canonical_resource, x_ms, content_length, "", range_str);
    req.headers.push_back({"Authorization", authorization});
  }

  req.url = std::move(url);
  if (!range_str.empty())
    req.headers.push_back({"Range", range_str});
  return req;
}

} // namespace points::converter
