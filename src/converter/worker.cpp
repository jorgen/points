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
#include "worker_p.h"

#include "threaded_event_loop_p.h"

namespace points
{
namespace converter
{
  
void worker_t::enqueue(threaded_event_loop_t &loop)
{
  (void)event_loop;
  _worker_request.data = this;
  uv_queue_work(
    loop.loop, &_worker_request,
    [](uv_work_t *req) { static_cast<worker_t *>(req->data)->work(); },
    [](uv_work_t *req, int status) {
      completion_t completion = status == UV_ECANCELED ? cancelled : completed;
      static_cast<worker_t *>(req->data)->after_work(completion);
    });
}
void worker_t::cancel()
{
  uv_cancel((uv_req_t*)&_worker_request);
}

}
} // namespace points
