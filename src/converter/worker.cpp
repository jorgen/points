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
#include "worker.hpp"

#include "threaded_event_loop.hpp"

#include <cassert>

namespace points::converter
{

worker_t::worker_t()
  : _done(false)
{
  async.data = this;
}

worker_t::~worker_t()
{
  assert(_done);
}
void worker_t::enqueue(threaded_event_loop_t &loop)
{
  loop.worker_thread_pool().enqueue([this, &loop] {
    this->work();
    loop.add_worker_done(this);
  });
}

} // namespace points::converter
