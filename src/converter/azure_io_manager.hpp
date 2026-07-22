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

// Azure Blob Storage using either Shared Key (account key) or a SAS token. Real Azure uses a
// virtual-host endpoint (https://<account>.blob.core.windows.net/<container>/<blob>); the local
// emulator (Azurite) puts the account in the path (http://127.0.0.1:10000/<account>/<container>/<blob>).
class azure_io_manager_t : public http_io_manager_t
{
public:
  struct config_t
  {
    bool https = true;
    std::string host;   // "<account>.blob.core.windows.net" or "127.0.0.1"
    uint16_t port = 0;  // 0 => default for the scheme
    std::string account;
    std::string account_key_base64; // Shared Key auth (base64). Empty when using a SAS.
    std::string sas;                // SAS query string without leading '?'. When set, used instead of Shared Key.
    std::string container;
    std::string prefix;             // blob-name prefix within the container (may be empty)
    bool account_in_path = false;   // Azurite: the account appears in the URL path
    std::string api_version = "2021-08-06";
  };

  azure_io_manager_t(vio::event_loop_t &event_loop, config_t cfg);

protected:
  vio::http::request_t build_request(const std::string &method, const std::string &name, std::span<const uint8_t> payload, const io_range_t *range) const override;

private:
  std::string blob_for(const std::string &name) const;
  config_t _cfg;
};

} // namespace points::converter
