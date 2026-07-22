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
#include "cloud_signing.hpp"

#include <vio/crypto.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <span>

namespace points::converter::cloud
{

static std::span<const uint8_t> bspan(std::string_view s)
{
  return {reinterpret_cast<const uint8_t *>(s.data()), s.size()};
}
static std::span<const uint8_t> bspan(const vio::crypto::sha256_digest_t &d)
{
  return {d.data(), d.size()};
}

static std::string to_lower(std::string_view s)
{
  std::string r(s);
  for (auto &c : r)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

static std::string trim(std::string_view s)
{
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t'))
    a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
    b--;
  return std::string(s.substr(a, b - a));
}

std::string uri_encode(std::string_view s, bool keep_slash)
{
  static const char *hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s)
  {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
      out.push_back(char(c));
    else if (c == '/' && keep_slash)
      out.push_back('/');
    else
    {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

std::string aws_sigv4_authorization(const std::string &method, const std::string &canonical_uri, const std::string &canonical_query, const std::vector<signed_header_t> &headers,
                                    const std::string &payload_sha256_hex, const std::string &access_key, const std::string &secret_key, const std::string &region, const std::string &service,
                                    const std::string &amz_date, const std::string &date_stamp)
{
  std::vector<std::pair<std::string, std::string>> hs;
  hs.reserve(headers.size());
  for (const auto &h : headers)
    hs.emplace_back(to_lower(h.name), trim(h.value));
  std::sort(hs.begin(), hs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

  std::string canonical_headers;
  std::string signed_headers;
  for (size_t i = 0; i < hs.size(); i++)
  {
    canonical_headers += hs[i].first + ":" + hs[i].second + "\n";
    if (i)
      signed_headers += ";";
    signed_headers += hs[i].first;
  }

  std::string canonical_request = method + "\n" + canonical_uri + "\n" + canonical_query + "\n" + canonical_headers + "\n" + signed_headers + "\n" + payload_sha256_hex;
  std::string cr_hash = vio::crypto::to_hex(vio::crypto::sha256(bspan(canonical_request)));

  std::string scope = date_stamp + "/" + region + "/" + service + "/aws4_request";
  std::string string_to_sign = std::string("AWS4-HMAC-SHA256\n") + amz_date + "\n" + scope + "\n" + cr_hash;

  std::string k0 = "AWS4" + secret_key;
  auto k_date = vio::crypto::hmac_sha256(bspan(k0), bspan(date_stamp));
  auto k_region = vio::crypto::hmac_sha256(bspan(k_date), bspan(region));
  auto k_service = vio::crypto::hmac_sha256(bspan(k_region), bspan(service));
  auto k_signing = vio::crypto::hmac_sha256(bspan(k_service), bspan(std::string_view("aws4_request")));
  std::string signature = vio::crypto::to_hex(vio::crypto::hmac_sha256(bspan(k_signing), bspan(string_to_sign)));

  return "AWS4-HMAC-SHA256 Credential=" + access_key + "/" + scope + ", SignedHeaders=" + signed_headers + ", Signature=" + signature;
}

std::string azure_sharedkey_authorization(const std::string &method, const std::string &account, const std::string &account_key_base64, const std::string &canonical_resource,
                                          const std::vector<signed_header_t> &x_ms_headers, const std::string &content_length, const std::string &content_type, const std::string &range)
{
  std::vector<std::pair<std::string, std::string>> hs;
  hs.reserve(x_ms_headers.size());
  for (const auto &h : x_ms_headers)
    hs.emplace_back(to_lower(h.name), trim(h.value));
  std::sort(hs.begin(), hs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

  std::string canonical_headers;
  for (const auto &h : hs)
    canonical_headers += h.first + ":" + h.second + "\n";

  // Blob service string-to-sign (fixed field order). Date is empty because we use x-ms-date.
  std::string sts = method + "\n"        // VERB
                    + "\n"               // Content-Encoding
                    + "\n"               // Content-Language
                    + content_length + "\n" // Content-Length ("" when zero)
                    + "\n"               // Content-MD5
                    + content_type + "\n" // Content-Type
                    + "\n"               // Date
                    + "\n"               // If-Modified-Since
                    + "\n"               // If-Match
                    + "\n"               // If-None-Match
                    + "\n"               // If-Unmodified-Since
                    + range + "\n"       // Range
                    + canonical_headers  // CanonicalizedHeaders (each already ends with \n)
                    + canonical_resource; // CanonicalizedResource

  auto key = vio::crypto::base64_decode(account_key_base64);
  if (!key.has_value())
    return {};
  auto sig = vio::crypto::hmac_sha256(std::span<const uint8_t>(key->data(), key->size()), bspan(sts));
  return "SharedKey " + account + ":" + vio::crypto::base64_encode(bspan(sig));
}

} // namespace points::converter::cloud
