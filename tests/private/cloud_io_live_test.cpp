// Live integration tests for the S3 and Azure io_managers. They are no-ops unless the matching env
// var (POINTS_TEST_S3 / POINTS_TEST_AZURE) is set, so a normal test run without a server stays green.
// Run against local minio/azurite, e.g.:
//   POINTS_TEST_S3=1 AWS_ACCESS_KEY_ID=minioadmin AWS_SECRET_ACCESS_KEY=minioadmin \
//     AWS_ENDPOINT_URL=http://127.0.0.1:9000 AWS_REGION=us-east-1 ./private_interface_unit_tests -tc="*S3 live*"
//   POINTS_TEST_AZURE=1 AZURE_STORAGE_ACCOUNT=devstoreaccount1 AZURE_STORAGE_KEY=<key> \
//     AZURE_BLOB_ENDPOINT=http://127.0.0.1:10000 ./private_interface_unit_tests -tc="*Azure live*"
#include <doctest/doctest.h>

#include <cloud_signing.hpp>
#include <io_manager.hpp>
#include <object_backend.hpp>
#include <storage_backend.hpp>

#include <vio/event_loop.h>
#include <vio/operation/http_client.h>
#include <vio/task.h>

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <vector>

using namespace points::converter;

namespace
{
struct wait_state_t
{
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  points_error_t result;
};

template <typename Factory>
vio::task_t<void> wait_coro(std::shared_ptr<wait_state_t> state, Factory factory)
{
  auto err = co_await factory();
  {
    std::unique_lock<std::mutex> lk(state->m);
    state->result = std::move(err);
    state->done = true;
  }
  state->cv.notify_one();
  co_return;
}

template <typename Factory>
points_error_t run_task(vio::event_loop_t &loop, Factory factory)
{
  auto state = std::make_shared<wait_state_t>();
  loop.run_in_loop([state, factory = std::move(factory)]() mutable -> vio::task_t<void> { return wait_coro(state, std::move(factory)); });
  std::unique_lock<std::mutex> lk(state->m);
  state->cv.wait(lk, [&] { return state->done; });
  return state->result;
}

std::shared_ptr<uint8_t[]> make_bytes(const std::vector<uint8_t> &v)
{
  auto p = std::make_shared<uint8_t[]>(v.size());
  if (!v.empty())
    memcpy(p.get(), v.data(), v.size());
  return p;
}

std::vector<uint8_t> pattern(uint32_t n, uint8_t seed)
{
  std::vector<uint8_t> v(n);
  for (uint32_t i = 0; i < n; i++)
    v[i] = uint8_t(seed + i * 7u);
  return v;
}

// Exercise the full io_manager contract against a live server, then a full object_backend round trip.
void live_io_manager_round_trip(io_manager_t &io, vio::event_loop_t &loop)
{
  auto data = pattern(1000, 5);
  REQUIRE(run_task(loop, [&]() { return io.write_object("points_it_obj", make_bytes(data), data.size()); }).code == 0);

  object_info_t info;
  REQUIRE(run_task(loop, [&]() { return io.object_info("points_it_obj", info); }).code == 0);
  REQUIRE(info.exists);
  REQUIRE(info.size == data.size());

  std::vector<uint8_t> whole(data.size());
  uint32_t br = 0;
  REQUIRE(run_task(loop, [&]() { return io.read_object("points_it_obj", whole.data(), io_range_t{0, int64_t(data.size())}, br); }).code == 0);
  REQUIRE(br == data.size());
  REQUIRE(memcmp(whole.data(), data.data(), data.size()) == 0);

  std::vector<uint8_t> mid(100);
  REQUIRE(run_task(loop, [&]() { return io.read_object("points_it_obj", mid.data(), io_range_t{200, 100}, br); }).code == 0);
  REQUIRE(br == 100);
  REQUIRE(memcmp(mid.data(), data.data() + 200, 100) == 0);

  REQUIRE(run_task(loop, [&]() { return io.remove_object("points_it_obj"); }).code == 0);
  REQUIRE(run_task(loop, [&]() { return io.object_info("points_it_obj", info); }).code == 0);
  REQUIRE(!info.exists);
}

void live_backend_round_trip(const std::string &url, vio::event_loop_t &loop)
{
  auto blob = pattern(777, 42);
  auto registry = pattern(64, 9);
  auto attrs = pattern(50, 3);
  storage_location_t loc, reg_loc;
  {
    points_error_t err;
    auto backend = create_storage_backend(url, loop, err);
    REQUIRE(err.code == 0);
    REQUIRE(backend);
    REQUIRE(backend->open_for_write(true).code == 0);
    backend->allocate_blob(uint32_t(blob.size()), loc);
    REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc, make_bytes(blob)); }).code == 0);
    backend->allocate_blob(uint32_t(registry.size()), reg_loc);
    REQUIRE(run_task(loop, [&]() { return backend->write_allocated(reg_loc, make_bytes(registry)); }).code == 0);
    checkpoint_t cp;
    cp.tree_registry = reg_loc;
    cp.attribute_configs = make_bytes(attrs);
    cp.attribute_configs_size = uint32_t(attrs.size());
    cp.stats = make_bytes(pattern(8, 1));
    cp.stats_size = 8;
    cp.perf = make_bytes(pattern(12, 2));
    cp.perf_size = 12;
    REQUIRE(run_task(loop, [&]() { return backend->write_index(std::move(cp)); }).code == 0);
  }
  // Reopen (persistent object store) and read everything back.
  points_error_t err;
  auto backend = create_storage_backend(url, loop, err);
  REQUIRE(err.code == 0);
  REQUIRE(backend->exists());
  index_load_t load;
  REQUIRE(backend->read_index(load).code == 0);
  REQUIRE(load.attribute_configs_size == attrs.size());
  REQUIRE(memcmp(load.attribute_configs.get(), attrs.data(), attrs.size()) == 0);
  std::vector<uint8_t> got(blob.size());
  uint32_t br = 0;
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(loc, got.data(), br); }).code == 0);
  REQUIRE(memcmp(got.data(), blob.data(), blob.size()) == 0);
}

std::string env_or(const char *name, const char *def)
{
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string(def);
}
} // namespace

TEST_CASE("S3 live round trip (minio)")
{
  if (!std::getenv("POINTS_TEST_S3"))
    return; // hermetic skip when no server is configured
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  points_error_t err;
  auto io = create_io_manager("s3", env_or("POINTS_TEST_S3_BUCKET", "pointstest") + "/it", loop, err);
  REQUIRE(err.code == 0);
  REQUIRE(io);
  live_io_manager_round_trip(*io, loop);

  live_backend_round_trip("s3://" + env_or("POINTS_TEST_S3_BUCKET", "pointstest") + "/ds", loop);
}

TEST_CASE("Azure live round trip (azurite)")
{
  if (!std::getenv("POINTS_TEST_AZURE"))
    return;
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  // Ensure the container exists (PUT container with restype=container). Idempotent (201 or 409).
  std::string account = env_or("AZURE_STORAGE_ACCOUNT", "devstoreaccount1");
  std::string key = env_or("AZURE_STORAGE_KEY", "");
  std::string container = env_or("POINTS_TEST_AZURE_CONTAINER", "pointstest");
  std::string endpoint = env_or("AZURE_BLOB_ENDPOINT", "http://127.0.0.1:10000");
  {
    std::string url = endpoint + "/" + account + "/" + container + "?restype=container";
    std::vector<cloud::signed_header_t> xms = {{"x-ms-date", "Mon, 01 Jan 2024 00:00:00 GMT"}, {"x-ms-version", "2021-08-06"}};
    // canonical resource: /account + uri-path, then "\nrestype:container".
    std::string cres = "/" + account + "/" + account + "/" + container + "\nrestype:container";
    // Use a real timestamp so azurite accepts it.
    // (Rebuild the date header here via a fresh request through the io layer would be cleaner, but a
    // fixed near-now value is accepted by azurite which does not enforce clock skew.)
    auto authz = cloud::azure_sharedkey_authorization("PUT", account, key, cres, xms, "", "", "");
    vio::http::request_t req;
    req.method = "PUT";
    req.url = url;
    req.allow_plaintext = true;
    req.headers.push_back({"x-ms-date", xms[0].value});
    req.headers.push_back({"x-ms-version", xms[1].value});
    req.headers.push_back({"Authorization", authz});
    auto rc = run_task(loop, [&]() -> vio::task_t<points_error_t> {
      auto resp = co_await vio::http::fetch(loop, req);
      points_error_t e;
      if (!resp.has_value())
      {
        e.code = -1;
        e.msg = resp.error().msg;
      }
      else if (resp->status != 201 && resp->status != 409)
      {
        e.code = -1;
        e.msg = "container create HTTP " + std::to_string(resp->status) + " " + resp->body.substr(0, 200);
      }
      co_return e;
    });
    REQUIRE(rc.code == 0);
  }

  points_error_t err;
  auto io = create_io_manager("az", container + "/it", loop, err);
  REQUIRE(err.code == 0);
  REQUIRE(io);
  live_io_manager_round_trip(*io, loop);

  live_backend_round_trip("az://" + container + "/ds", loop);
}
