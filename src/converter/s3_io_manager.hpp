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

#include "http_io_manager.hpp"

#include <cstdint>
#include <string>

namespace points::converter
{

// S3-compatible object store (AWS S3, minio, ...) using AWS Signature Version 4. Supports both
// path-style (https://endpoint/bucket/key, used by minio and custom endpoints) and virtual-host style
// (https://bucket.endpoint/key, the AWS default).
class s3_io_manager_t : public http_io_manager_t
{
public:
  struct config_t
  {
    bool https = true;
    std::string host;    // endpoint host, e.g. "s3.us-east-1.amazonaws.com" or "127.0.0.1"
    uint16_t port = 0;   // 0 => default for the scheme
    std::string region = "us-east-1";
    std::string bucket;
    std::string prefix;  // key prefix within the bucket (may be empty; no leading/trailing '/')
    std::string access_key;
    std::string secret_key;
    bool path_style = false;
  };

  s3_io_manager_t(vio::event_loop_t &event_loop, config_t cfg);

protected:
  vio::http::request_t build_request(const std::string &method, const std::string &name, std::span<const uint8_t> payload, const io_range_t *range) const override;

private:
  std::string key_for(const std::string &name) const;
  config_t _cfg;
};

} // namespace points::converter
