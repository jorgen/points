/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/

// Querying a dewfall dataset from a vio event loop, with coroutines.
//
//   query_async <dataset-url> [minx,miny,minz,maxx,maxy,maxz] [--connection SPEC]
//
// The point of this example is the SHAPE of the code, not what it prints. Compare:
//
//   dew_request_t *r = dew_dataset_request_region(dataset, &spec, &error);
//   dew_request_wait(r, -1);                                  // parks the thread
//
// against what run_query() below actually writes:
//
//   dew_request_t *r = dew_dataset_request_region(dataset, &spec, &error);
//   co_await awaiter;                                         // frees the thread
//
// Same sequence, same readability, but the second one hands the thread back to the event loop while
// the IO is outstanding. That is what lets one loop have several queries in flight (run_query is
// spawned twice here, deliberately, and their reads interleave), and it is what makes the API usable
// from a program that already has a loop it must not block -- a server, a renderer, a browser.
//
// dew_vio_await.h holds the entire adapter. Read that first; this file is just a consumer of it.

#include "dew_vio_await.h"

#include <dew/access/query.h>
#include <dew/core/error.h>
#include <dew/core/pump.h>

#include <vio/event_loop.h>
#include <vio/task.h>

#include <fmt/format.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct args_t
{
  std::string url;
  std::string connection;
  double aabb_min[3] = {0, 0, 0};
  double aabb_max[3] = {0, 0, 0};
  bool has_aabb = false;
};

bool parse_aabb(const char *text, double (&min)[3], double (&max)[3])
{
  double values[6];
  const char *cursor = text;
  for (int i = 0; i < 6; i++)
  {
    char *end = nullptr;
    values[i] = std::strtod(cursor, &end); // strtod, not std::stod: the project builds -fno-exceptions
    if (end == cursor)
      return false;
    cursor = (i < 5) ? (*end == ',' ? end + 1 : end) : end;
    if (i < 5 && *end != ',')
      return false;
  }
  for (int i = 0; i < 3; i++)
  {
    min[i] = values[i];
    max[i] = values[i + 3];
  }
  return true;
}

void print_error(const char *prefix, dew_error_t *error)
{
  if (!error)
  {
    fmt::print(stderr, "{}\n", prefix);
    return;
  }
  int code = 0;
  const char *text = nullptr;
  size_t length = 0;
  dew_error_get_info(error, &code, &text, &length);
  fmt::print(stderr, "{}: {} ({})\n", prefix, std::string(text ? text : "", length), code);
  dew_error_destroy(error);
}

// One query, start to finish, without ever blocking the loop. Note what is NOT a parameter: the
// awaiter needs no reference to the driver, because a request already has a completion callback. Only
// dataset OPEN needs the driver, since readiness is a polled state rather than a callback.
vio::task_t<void> run_query(dew_dataset_t *dataset, const char *label, const double (&aabb_min)[3], const double (&aabb_max)[3], dew_lod_mode_t lod_mode, uint64_t max_points)
{
  static const char *const attribute_names[] = {"intensity"};

  // The awaiter is a NAMED local, not a temporary: the done callback writes into it, so it has to
  // outlive the request. See the note in dew_vio_await.h.
  dew_await::request_awaiter_t awaiter;

  dew_region_request_t spec = {};
  memcpy(spec.aabb_min, aabb_min, sizeof(spec.aabb_min));
  memcpy(spec.aabb_max, aabb_max, sizeof(spec.aabb_max));
  spec.lod_mode = lod_mode;
  spec.max_points = max_points;
  spec.attribute_names = attribute_names;
  spec.attribute_count = 1;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_point;
  spec.done = dew_await::request_awaiter_t::done();
  spec.done_user_ptr = awaiter.user_ptr();

  dew_error_t *error = nullptr;
  dew_request_t *request = dew_dataset_request_region(dataset, &spec, &error);
  if (!request)
  {
    print_error(fmt::format("[{}] could not start the request", label).c_str(), error);
    co_return;
  }

  fmt::print("[{}] issued, status={} -- returning to the loop\n", label, int(dew_request_status(request)));

  // The line this example exists for. The thread goes back to the loop; the other query proceeds.
  dew_request_status_t status = co_await awaiter;

  if (status != dew_request_completed)
  {
    dew_error_t *request_error = nullptr;
    dew_request_get_error(request, &request_error);
    print_error(fmt::format("[{}] request failed (status {})", label, int(status)).c_str(), request_error);
    dew_request_release(request);
    co_return;
  }

  dew_request_result_t result = {};
  if (!dew_request_get_result(request, &result))
  {
    fmt::print(stderr, "[{}] no result\n", label);
    dew_request_release(request);
    co_return;
  }

  fmt::print("[{}] {} points across {} node(s)\n", label, result.point_count, result.node_count);
  for (uint32_t i = 0; i < result.buffer_count; i++)
  {
    const auto &buffer = result.buffers[i];
    // Buffer 0 is always the positions and carries no name; requested attributes start at 1.
    fmt::print("[{}]   buffer {}: {:14} {} bytes\n", label, i, i == 0 ? "xyz" : std::string(buffer.name, buffer.name_size), buffer.size_bytes);
  }
  if (result.point_count > 0 && result.buffer_count > 0)
  {
    const auto *xyz = static_cast<const double *>(result.buffers[0].data);
    fmt::print("[{}]   first point: [{:.3f}, {:.3f}, {:.3f}]\n", label, xyz[0], xyz[1], xyz[2]);
  }

  // Releasing invalidates every pointer in `result`, so it goes last.
  dew_request_release(request);
}

// Open the dataset, then run two overlapping queries on it.
vio::task_t<void> run(dew_await::loop_t &driver, const args_t &args, int &exit_code)
{
  dew_error_t *error = nullptr;
  dew_dataset_t *dataset = dew_dataset_create(args.url.c_str(), uint32_t(args.url.size()), args.connection.c_str(), uint32_t(args.connection.size()), nullptr, driver.pump(), &error);
  if (!dataset)
  {
    print_error("could not create the dataset", error);
    exit_code = 1;
    driver.event_loop().stop();
    co_return;
  }

  // dew_dataset_create returns immediately, always. Even a local file is still `opening` here: the
  // index read happens on the dataset's own loop.
  fmt::print("opening (state={}) -- nothing has blocked\n", int(dew_dataset_state(dataset)));

  if (co_await dew_await::dataset_ready(driver, dataset) != dew_dataset_ready)
  {
    dew_error_t *open_error = nullptr;
    dew_dataset_get_error(dataset, &open_error);
    print_error("could not open the dataset", open_error);
    dew_dataset_close(dataset);
    exit_code = 1;
    driver.event_loop().stop();
    co_return;
  }

  dew_dataset_info_t info = {};
  dew_dataset_get_info(dataset, &info);
  fmt::print("ready: cell [{:.3f}, {:.3f}, {:.3f}] .. [{:.3f}, {:.3f}, {:.3f}], scale {}\n", info.aabb_min[0], info.aabb_min[1], info.aabb_min[2], info.aabb_max[0], info.aabb_max[1], info.aabb_max[2],
             info.scale);

  double whole_min[3];
  double whole_max[3];
  for (int i = 0; i < 3; i++)
  {
    whole_min[i] = args.has_aabb ? args.aabb_min[i] : info.aabb_min[i];
    whole_max[i] = args.has_aabb ? args.aabb_max[i] : info.aabb_max[i];
  }

  // Two queries over the same box: a coarse preview and the full-resolution fetch. That pairing is
  // why the async shape earns its keep -- a UI wants something on screen immediately and the real data
  // when it arrives, and here the preview genuinely completes first (watch the output order) because
  // it reads far fewer nodes.
  //
  // Spawn both, THEN await both. Awaiting the first before starting the second would serialize them
  // and make the whole exercise pointless.
  auto preview = run_query(dataset, "preview", whole_min, whole_max, dew_lod_point_budget, 20000);
  auto full = run_query(dataset, "full", whole_min, whole_max, dew_lod_full, 0);
  co_await std::move(preview);
  co_await std::move(full);

  dew_dataset_close(dataset);
  driver.event_loop().stop();
  co_return;
}

} // namespace

int main(int argc, char **argv)
{
  args_t args;
  for (int i = 1; i < argc; i++)
  {
    std::string argument = argv[i];
    if (argument == "--connection" && i + 1 < argc)
    {
      args.connection = argv[++i];
    }
    else if (argument == "--help" || argument == "-h")
    {
      fmt::print("usage: query_async <dataset-url> [minx,miny,minz,maxx,maxy,maxz] [--connection SPEC]\n");
      return 0;
    }
    else if (args.url.empty())
    {
      args.url = argument;
    }
    else if (!args.has_aabb)
    {
      if (!parse_aabb(argument.c_str(), args.aabb_min, args.aabb_max))
      {
        fmt::print(stderr, "could not parse the box: {}\n", argument);
        return 2;
      }
      args.has_aabb = true;
    }
  }
  if (args.url.empty())
  {
    fmt::print(stderr, "usage: query_async <dataset-url> [minx,miny,minz,maxx,maxy,maxz] [--connection SPEC]\n");
    return 2;
  }

  vio::event_loop_t loop;
  dew_await::loop_t driver(loop);
  int exit_code = 0;

  // Kick the work off ON the loop, then run it. run() suspends at its first co_await and control
  // returns here, into loop.run(), which is where everything actually happens.
  loop.run_in_loop([&driver, &args, &exit_code]() -> vio::task_t<void> { return run(driver, args, exit_code); });
  loop.run();

  return exit_code;
}
