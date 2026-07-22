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
#pragma once

#include "io_manager.hpp"

#include <vio/operation/http_client.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace points::converter
{

// Common HTTP object-store machinery for the S3 and Azure backends: it implements the four io_manager
// operations as GET / PUT / HEAD / DELETE over vio::http::fetch and interprets the responses. Providers
// supply a fully-signed request via build_request(); everything else (status handling, copying the body
// out, mapping to points_error_t) lives here.
class http_io_manager_t : public io_manager_t
{
public:
  explicit http_io_manager_t(vio::event_loop_t &event_loop);

  vio::task_t<points_error_t> read_object(std::string name, uint8_t *dst, io_range_t range, uint32_t &bytes_read) override;
  vio::task_t<points_error_t> write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size) override;
  vio::task_t<points_error_t> object_info(std::string name, object_info_t &out) override;
  vio::task_t<points_error_t> remove_object(std::string name) override;

protected:
  // Build and sign the request for the given op. `payload` is the exact body (empty for GET/HEAD/DELETE);
  // `range` is non-null only for a ranged GET. Sets url/method/headers/body and, from the members below,
  // allow_plaintext and ca_mem.
  virtual vio::http::request_t build_request(const std::string &method, const std::string &name, std::span<const uint8_t> payload, const io_range_t *range) const = 0;

  // The Host header value vio::http::fetch will send for (scheme, host, port): host, plus ":port" only
  // when the port is non-default. Providers must sign this exact value.
  static std::string host_header(bool https, const std::string &host, uint16_t port);
  // Current UTC time in the formats the providers need.
  static void utc_now(std::string &amz_date, std::string &date_stamp, std::string &rfc1123_date);

  vio::event_loop_t &_event_loop;
  bool _allow_plaintext = false;              // permit http:// (e.g. a local minio/azurite over plain HTTP)
  std::optional<std::vector<uint8_t>> _ca_mem; // optional custom CA bundle for a private endpoint

private:
  vio::task_t<points_error_t> do_request(vio::http::request_t req, vio::http::response_t &out) const;
};

} // namespace points::converter
