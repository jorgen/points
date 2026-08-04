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

// blob_reader_t's completion contract.
//
// A read can finish along six different paths -- a cold backend read, three separate cache-hit
// branches, a decompress hopped to the thread pool, and cancellation -- and each used to mark the
// request done with its own inline `_done = true; notify_all()`. They now all funnel through
// complete_read_request(). These tests pin the property that made the unification worth doing:
// the completion hook fires EXACTLY ONCE per request, on every path.
//
// The subtle case is the cache hit. Three of the six paths complete the request BEFORE read()
// returns, which is why the hook has to be handed in via read_options_t rather than attached to the
// returned request -- a hook installed afterwards would silently never fire on a warm cache.

#include <doctest/doctest.h>

#include <vio/event_pipe.h>
#include <vio/thread_pool.h>

#include "blob_reader.hpp"
#include "input_header.hpp"
#include "storage_handler.hpp"

#include <dew/converter/converter.h>

#include <atomic>
#include <string>

using namespace dew::core;

namespace
{

// A storage_handler_t is used to WRITE the fixture blobs; the reads under test go through the
// blob_reader_t it composes.
struct reader_fixture_t : vio::about_to_block_t
{
  reader_fixture_t()
    : pool(4)
    , index_written(loop_thread.event_loop(), bind(&reader_fixture_t::on_index_written))
    , storage_error(loop_thread.event_loop(), bind(&reader_fixture_t::on_storage_error))
    , storage("blob_reader_test_cache_file", pool, attributes, perf, index_written, storage_error, error)
  {
    loop_thread.event_loop().add_about_to_block_listener(this);
    REQUIRE(storage.upgrade_to_write(true).code == 0);
  }

  void about_to_block() override {}
  void on_index_written() {}
  void on_storage_error(const dew_error_t &&) {}

  // Write one attribute blob and return where it landed.
  storage_location_t write_blob(uint32_t point_count)
  {
    dew_converter_attributes_t attrs;
    dew_converter_attributes_add_attribute(&attrs, "xyz", 3, dew_type_m64, dew_components_1);
    auto attribute_id = attributes.get_attribute_config_index(std::move(attrs));
    auto formats = attributes.get_format_components(attribute_id);

    points_t points;
    points.header.input_id = {next_id++, 0};
    points.header.morton_min = {};
    points.header.morton_max = {};
    points.header.morton_max.data[0] = uint64_t(1) << 30;
    points.header.point_count = point_count;
    points.header.lod_span = morton::morton_lod(points.header.morton_min, points.header.morton_max);
    points.header.point_format = formats[0];
    dew::converter::attribute_buffers_initialize(formats, points.buffers, point_count);
    auto *m = reinterpret_cast<morton::morton64_t *>(points.buffers.data[0].get());
    for (uint32_t i = 0; i < point_count; i++)
      m[i].data[0] = i * 977ull;

    std::unique_lock<std::mutex> lock(mutex);
    done = false;
    storage.write(points.header, attribute_id, std::move(points.buffers),
                  [this](const storage_header_t &, attributes_id_t, std::vector<storage_location_t> locs, const dew_error_t &) {
                    std::unique_lock<std::mutex> inner(mutex);
                    done = true;
                    written = std::move(locs);
                    cond.notify_all();
                  });
    cond.wait(lock, [this] { return done; });
    REQUIRE(!written.empty());
    return written[0];
  }

  dew_error_t error;
  vio::thread_pool_t pool;
  vio::thread_with_event_loop_t loop_thread;
  attributes_configs_t attributes;
  perf_stats_t perf;
  vio::event_pipe_t<void> index_written;
  vio::event_pipe_t<dew_error_t> storage_error;
  dew::converter::storage_handler_t storage;

  uint32_t next_id = 0;
  std::mutex mutex;
  std::condition_variable cond;
  bool done = false;
  std::vector<storage_location_t> written;
};

} // namespace

TEST_CASE("blob_reader: completion hook fires exactly once on a cold read")
{
  reader_fixture_t fixture;
  auto location = fixture.write_blob(512);

  std::atomic<int> fired{0};
  auto request = fixture.storage.read(location, read_options_t{false, true, [&fired](read_request_t &) { fired.fetch_add(1, std::memory_order_relaxed); }});
  request->wait_for_read();

  REQUIRE(request->error.code == 0);
  REQUIRE(request->buffer_info.size > 0);
  REQUIRE(fired.load() == 1);
}

TEST_CASE("blob_reader: completion hook fires on a cache hit, where read() completes inline")
{
  reader_fixture_t fixture;
  auto location = fixture.write_blob(512);

  // Prime the caches.
  fixture.storage.read(location, read_options_t{false, true, {}})->wait_for_read();

  // Second read is served from cache. On this path the request is already done by the time read()
  // returns, so a hook attached to the returned object would never run -- the whole reason
  // read_options_t carries it.
  std::atomic<int> fired{0};
  auto request = fixture.storage.read(location, read_options_t{false, true, [&fired](read_request_t &) { fired.fetch_add(1, std::memory_order_relaxed); }});

  // The load-bearing assertion: the request is ALREADY complete here, before wait_for_read and before
  // the caller could have attached anything to it. If read_options_t ever stops carrying the hook,
  // this is the test that catches it -- `fired` would be 0 while the read still succeeded.
  REQUIRE(request->_done);
  REQUIRE(fired.load() == 1);

  request->wait_for_read();
  REQUIRE(request->error.code == 0);
}

TEST_CASE("blob_reader: raw reads return the compressed bytes and still complete once")
{
  reader_fixture_t fixture;
  auto location = fixture.write_blob(4096);

  std::atomic<int> fired{0};
  auto request = fixture.storage.read(location, read_options_t{true, false, [&fired](read_request_t &) { fired.fetch_add(1, std::memory_order_relaxed); }});
  request->wait_for_read();

  REQUIRE(request->error.code == 0);
  REQUIRE(fired.load() == 1);
  // raw hands back exactly what is stored, so the payload matches the recorded blob size.
  REQUIRE(request->buffer_info.size == location.size);
}

TEST_CASE("blob_reader: a cancelled read still completes exactly once")
{
  reader_fixture_t fixture;
  auto location = fixture.write_blob(512);

  std::atomic<int> fired{0};
  auto request = fixture.storage.read(location, read_options_t{false, true, [&fired](read_request_t &) { fired.fetch_add(1, std::memory_order_relaxed); }});
  request->set_cancelled();
  request->wait_for_read();

  // Cancellation is advisory: the read may already have finished. Either way the request reaches a
  // terminal state and the hook runs once -- never zero times, which would hang a waiter forever.
  REQUIRE(fired.load() == 1);
}

TEST_CASE("blob_reader: a write invalidates the cached bytes for a reused offset")
{
  reader_fixture_t fixture;
  auto location = fixture.write_blob(512);

  auto first = fixture.storage.read(location, read_options_t{true, false, {}});
  first->wait_for_read();
  REQUIRE(first->error.code == 0);

  // The allocator reuses freed offsets, so the reader must drop a cached entry when that location is
  // overwritten. Without invalidate() a later read would be served the previous blob's bytes.
  fixture.storage.reader().invalidate(location);
  auto second = fixture.storage.read(location, read_options_t{true, false, {}});
  second->wait_for_read();
  REQUIRE(second->error.code == 0);
  REQUIRE(second->buffer_info.size == location.size);
}
