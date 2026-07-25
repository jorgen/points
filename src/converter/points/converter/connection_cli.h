/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2025  Jørgen Lind
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
#ifndef POINTS_CONVERTER_CONNECTION_CLI_H
#define POINTS_CONVERTER_CONNECTION_CLI_H

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace points
{
namespace converter
{
namespace cli
{

// Does a connection string appear to carry a secret value (one that should not be world-visible)? Used
// only to decide whether to warn about an inline (argv) connection string.
inline bool connection_string_has_secret(const std::string &connection)
{
  std::string lower(connection);
  for (char &c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const char *const markers[] = {"secret", "token", "account_key", "accountkey", "shared_access_signature", "sharedaccesssignature", "sas=", "password"};
  for (const char *marker : markers)
    if (lower.find(marker) != std::string::npos)
      return true;
  return false;
}

#ifndef __EMSCRIPTEN__
// For a plain AWS s3:// URL with no credentials otherwise supplied (not in the connection string, not in
// AWS_* env, and no custom endpoint), resolve credentials from the AWS CLI's OWN provider chain -- shared
// config/credentials, SSO, `aws login`, assume-role, credential_process; respecting AWS_PROFILE -- by
// running `aws configure export-credentials`, and set them in this process's environment for the vio S3
// signer to pick up. Mirrors what `aws s3` does: read ~/.aws and "do the right thing". A silent no-op if
// credentials are already available, the URL isn't plain AWS s3, or the `aws` CLI is missing/fails (vio
// then reports "credentials missing"). The `aws` subprocess command is a fixed string (no injection); the
// resolved secrets go into the environment (owner/root-readable, NOT visible via `ps`).
inline void apply_aws_cli_credentials(const std::string &url, const std::string &connection)
{
#if defined(_WIN32)
  (void)url;
  (void)connection; // the ~/.aws fallback shells out to the `aws` CLI via popen -- POSIX only for now
#else
  if (url.rfind("s3://", 0) != 0)
    return;
  if (std::getenv("AWS_ACCESS_KEY_ID") != nullptr)
    return; // credentials already in the environment
  if (std::getenv("AWS_ENDPOINT_URL") != nullptr || std::getenv("AWS_S3_ENDPOINT") != nullptr)
    return; // a custom endpoint (e.g. minio) is configured -- not plain AWS
  {
    std::string lower(connection);
    for (char &c : lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("access_key_id") != std::string::npos || lower.find("accesskeyid") != std::string::npos)
      return; // credentials supplied in the connection string
    if (lower.find("endpoint") != std::string::npos)
      return; // custom endpoint in the connection string
  }

  FILE *pipe = ::popen("aws configure export-credentials --format env-no-export 2>/dev/null", "r");
  if (pipe == nullptr)
    return;
  char line[8192];
  while (std::fgets(line, sizeof(line), pipe) != nullptr)
  {
    std::string entry(line);
    while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r'))
      entry.pop_back();
    const auto eq = entry.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = entry.substr(0, eq);
    const std::string value = entry.substr(eq + 1);
    if (key == "AWS_ACCESS_KEY_ID" || key == "AWS_SECRET_ACCESS_KEY" || key == "AWS_SESSION_TOKEN")
      ::setenv(key.c_str(), value.c_str(), 1);
  }
  ::pclose(pipe);

  // Fill the region from the CLI config if the caller didn't set it (AWS needs the right region to sign).
  if (std::getenv("AWS_REGION") == nullptr && std::getenv("AWS_DEFAULT_REGION") == nullptr)
  {
    FILE *region_pipe = ::popen("aws configure get region 2>/dev/null", "r");
    if (region_pipe != nullptr)
    {
      char region_buf[256];
      if (std::fgets(region_buf, sizeof(region_buf), region_pipe) != nullptr)
      {
        std::string region(region_buf);
        while (!region.empty() && (region.back() == '\n' || region.back() == '\r' || region.back() == ' ' || region.back() == '\t'))
          region.pop_back();
        if (!region.empty())
          ::setenv("AWS_REGION", region.c_str(), 1);
      }
      ::pclose(region_pipe);
    }
  }
#endif // _WIN32
}
#endif // __EMSCRIPTEN__

// Resolve a connection-string command-line argument (--connection / --source-connection /
// --destination-connection) into the actual connection string, so credentials never need to sit in argv
// (which is world-visible via `ps aux`):
//   "@path"        -> the contents of a file (recommended chmod 600); trailing whitespace is stripped
//   "env:NAME"     -> the value of environment variable NAME
//   anything else  -> the literal string (an inline connection string, or "")
// An inline string carrying a secret warns to stderr (prefer @file / env:VAR so it stays out of argv).
// On success returns true and fills `out`; on failure returns false and sets `error`.
inline bool resolve_connection_spec(const std::string &spec, std::string &out, std::string &error)
{
  if (!spec.empty() && spec[0] == '@')
  {
    const std::string path = spec.substr(1);
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
      error = "cannot open connection file: " + path;
      return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '\t'))
      out.pop_back();
    return true;
  }
  if (spec.rfind("env:", 0) == 0)
  {
    const std::string name = spec.substr(4);
    const char *value = std::getenv(name.c_str());
    if (!value)
    {
      error = "environment variable not set: " + name;
      return false;
    }
    out = value;
    return true;
  }
  out = spec; // inline connection string (or "")
  if (connection_string_has_secret(out))
    std::fprintf(stderr, "warning: credentials on the command line are visible to other users via `ps`; prefer '@file' or 'env:VAR'\n");
  return true;
}

} // namespace cli
} // namespace converter
} // namespace points

#endif // POINTS_CONVERTER_CONNECTION_CLI_H
