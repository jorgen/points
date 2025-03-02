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

namespace points::converter
{
class event_loop_t;
class worker_t
{
public:
  enum completion_t
  {
    cancelled,
    completed
  };
  worker_t();
  virtual ~worker_t();
  virtual void work() = 0;
  virtual void after_work(completion_t completion) = 0;

  void enqueue(event_loop_t &loop);
  void mark_done()
  {
    _done = true;
  }
  bool done() const
  {
    return _done;
  }

private:
  bool _done;
  uv_async_t async;
};
} // namespace points::converter
