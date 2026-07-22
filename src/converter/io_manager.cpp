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
#include "io_manager.hpp"

#include "azure_io_manager.hpp"
#include "file_dir_io_manager.hpp"
#include "memory_io_manager.hpp"
#include "s3_io_manager.hpp"

#include <cstdlib>

namespace points::converter
{

static std::string getenv_str(const char *name)
{
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

// Parse "scheme://host[:port]" (any trailing path is ignored). Returns false if malformed.
static bool parse_endpoint(const std::string &url, bool &https, std::string &host, uint16_t &port)
{
  auto sep = url.find("://");
  if (sep == std::string::npos)
    return false;
  https = url.substr(0, sep) == "https";
  std::string rest = url.substr(sep + 3);
  auto slash = rest.find('/');
  std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
  auto colon = hostport.find(':');
  if (colon == std::string::npos)
  {
    host = hostport;
    port = 0;
  }
  else
  {
    host = hostport.substr(0, colon);
    port = uint16_t(std::atoi(hostport.substr(colon + 1).c_str()));
  }
  return !host.empty();
}

// Split the URL body into the first path segment (bucket/container) and the remaining prefix.
static void split_bucket_prefix(const std::string &path, std::string &bucket, std::string &prefix)
{
  auto slash = path.find('/');
  if (slash == std::string::npos)
  {
    bucket = path;
    prefix.clear();
  }
  else
  {
    bucket = path.substr(0, slash);
    prefix = path.substr(slash + 1);
  }
  while (!prefix.empty() && prefix.back() == '/')
    prefix.pop_back();
}

static std::unique_ptr<io_manager_t> create_s3(const std::string &path, vio::event_loop_t &loop, points_error_t &error)
{
  s3_io_manager_t::config_t cfg;
  split_bucket_prefix(path, cfg.bucket, cfg.prefix);
  if (cfg.bucket.empty())
  {
    error = {-1, "s3 url missing bucket (expected s3://bucket/prefix)"};
    return nullptr;
  }
  cfg.access_key = getenv_str("AWS_ACCESS_KEY_ID");
  cfg.secret_key = getenv_str("AWS_SECRET_ACCESS_KEY");
  cfg.region = getenv_str("AWS_REGION");
  if (cfg.region.empty())
    cfg.region = getenv_str("AWS_DEFAULT_REGION");
  if (cfg.region.empty())
    cfg.region = "us-east-1";

  std::string endpoint = getenv_str("AWS_ENDPOINT_URL");
  if (endpoint.empty())
    endpoint = getenv_str("AWS_S3_ENDPOINT");
  if (!endpoint.empty())
  {
    if (!parse_endpoint(endpoint, cfg.https, cfg.host, cfg.port))
    {
      error = {-1, "invalid AWS_ENDPOINT_URL"};
      return nullptr;
    }
    cfg.path_style = true; // custom endpoints (minio) default to path-style
  }
  else
  {
    cfg.https = true;
    cfg.host = "s3." + cfg.region + ".amazonaws.com";
    cfg.path_style = false;
  }
  std::string fps = getenv_str("AWS_S3_FORCE_PATH_STYLE");
  if (fps == "1" || fps == "true" || fps == "TRUE")
    cfg.path_style = true;

  if (cfg.access_key.empty() || cfg.secret_key.empty())
  {
    error = {-1, "s3: set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY"};
    return nullptr;
  }
  return std::make_unique<s3_io_manager_t>(loop, std::move(cfg));
}

static std::unique_ptr<io_manager_t> create_azure(const std::string &path, vio::event_loop_t &loop, points_error_t &error)
{
  azure_io_manager_t::config_t cfg;
  split_bucket_prefix(path, cfg.container, cfg.prefix);
  if (cfg.container.empty())
  {
    error = {-1, "azure url missing container (expected az://container/prefix)"};
    return nullptr;
  }
  cfg.account = getenv_str("AZURE_STORAGE_ACCOUNT");
  cfg.account_key_base64 = getenv_str("AZURE_STORAGE_KEY");
  cfg.sas = getenv_str("AZURE_STORAGE_SAS");
  if (cfg.account.empty())
  {
    error = {-1, "azure: set AZURE_STORAGE_ACCOUNT"};
    return nullptr;
  }

  std::string endpoint = getenv_str("AZURE_BLOB_ENDPOINT");
  if (endpoint.empty())
    endpoint = getenv_str("AZURE_STORAGE_ENDPOINT");
  if (!endpoint.empty())
  {
    if (!parse_endpoint(endpoint, cfg.https, cfg.host, cfg.port))
    {
      error = {-1, "invalid AZURE_BLOB_ENDPOINT"};
      return nullptr;
    }
    cfg.account_in_path = true; // azurite / custom endpoints carry the account in the path
  }
  else
  {
    cfg.https = true;
    cfg.host = cfg.account + ".blob.core.windows.net";
    cfg.account_in_path = false;
  }

  if (cfg.sas.empty() && cfg.account_key_base64.empty())
  {
    error = {-1, "azure: set AZURE_STORAGE_KEY or AZURE_STORAGE_SAS"};
    return nullptr;
  }
  return std::make_unique<azure_io_manager_t>(loop, std::move(cfg));
}

std::unique_ptr<io_manager_t> create_io_manager(const std::string &scheme, const std::string &path, vio::event_loop_t &event_loop, points_error_t &error)
{
  if (scheme == "dir")
    return std::make_unique<file_dir_io_manager_t>(path, event_loop);
  if (scheme == "mem")
    return std::make_unique<memory_io_manager_t>();
  if (scheme == "s3")
    return create_s3(path, event_loop, error);
  if (scheme == "az" || scheme == "azure")
    return create_azure(path, event_loop, error);

  error.code = -1;
  error.msg = "Unsupported io_manager scheme: '" + scheme + "'";
  return nullptr;
}

} // namespace points::converter
