/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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

#include <uv.h>

namespace points
{
namespace converter
{
class threaded_event_loop_t;
class worker_t
{
public:
  enum completion_t
  {
    cancelled,
    completed 
  };
  virtual ~worker_t() {}
  virtual void work() = 0;
  virtual void after_work(completion_t completion) = 0;

  void enqueue(threaded_event_loop_t &loop);
  void cancel();

private:
  uv_work_t _worker_request;
  threaded_event_loop_t *event_loop;
};
}
} // namespace points
