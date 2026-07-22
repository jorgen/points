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

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Request signing for object-store backends: AWS Signature Version 4 (S3) and Azure Blob Shared Key.
// Pure functions over vio::crypto primitives so they are unit-testable against published vectors.
namespace points::converter::cloud
{

struct signed_header_t
{
  std::string name;
  std::string value;
};

// RFC 3986 percent-encoding. When keep_slash is true, '/' is left unescaped (for a canonical URI path);
// unreserved characters A-Za-z0-9 - _ . ~ are never escaped.
std::string uri_encode(std::string_view s, bool keep_slash);

// AWS Signature Version 4. Returns the value for the Authorization header. `headers` must contain
// exactly the headers to be signed (e.g. host, x-amz-date, x-amz-content-sha256) and must also be sent
// on the wire. canonical_uri is the already-encoded path; canonical_query is "" for a plain object op.
std::string aws_sigv4_authorization(const std::string &method, const std::string &canonical_uri, const std::string &canonical_query, const std::vector<signed_header_t> &headers,
                                    const std::string &payload_sha256_hex, const std::string &access_key, const std::string &secret_key, const std::string &region, const std::string &service,
                                    const std::string &amz_date, const std::string &date_stamp);

// Azure Blob "Shared Key" authorization. Returns "SharedKey <account>:<base64-signature>".
// canonical_resource is "/<account>/<container>/<blob>" (plus any sorted query, "\nname:value").
// x_ms_headers are the x-ms-* headers to canonicalize (e.g. x-ms-date, x-ms-version, x-ms-blob-type).
// content_length is "" for GET/HEAD/DELETE (and for a zero-length body); range is "" or "bytes=a-b".
std::string azure_sharedkey_authorization(const std::string &method, const std::string &account, const std::string &account_key_base64, const std::string &canonical_resource,
                                          const std::vector<signed_header_t> &x_ms_headers, const std::string &content_length, const std::string &content_type, const std::string &range);

} // namespace points::converter::cloud
