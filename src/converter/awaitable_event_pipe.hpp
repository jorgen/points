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
#pragma once
#include "threaded_event_loop.hpp"

namespace points::converter {
  template<typename RET, typename... ARGS>
  class awaitable_event_pipe_t {
  public:
    awaitable_event_pipe_t(event_loop_t &request_loop, event_loop_t &response_loop,
                           std::function<RET(ARGS...)> &&function) {
      (void) request_loop;
      (void) response_loop;
      (void) function;
    }

  private:
    event_pipe_t<ARGS...> request_pipe;
    event_pipe_t<RET> response_pipe;
  };
} // namespace points::converter
