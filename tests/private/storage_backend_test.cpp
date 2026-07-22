#include <doctest/doctest.h>

#include <io_manager.hpp>
#include <memory_io_manager.hpp>
#include <file_dir_io_manager.hpp>
#include <object_backend.hpp>
#include <storage_backend.hpp>
#include <index_format.hpp>
#include <conversion_types.hpp>

#include <vio/event_loop.h>
#include <vio/task.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace points::converter;

namespace
{
struct run_task_state_t
{
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  points_error_t result;
};

// A real coroutine taking state/factory BY VALUE (copied into the frame). A lambda coroutine would
// keep them in the closure temporary, which dies after the first suspension -> use-after-free.
template <typename Factory>
vio::task_t<void> run_task_coro(std::shared_ptr<run_task_state_t> state, Factory factory)
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

// Run a coroutine (returning points_error_t) on `loop` from the main (test) thread and block until
// it finishes. Mirrors the production bootstrap pattern (loop on its own thread, callers elsewhere).
template <typename Factory>
points_error_t run_task(vio::event_loop_t &loop, Factory factory)
{
  auto state = std::make_shared<run_task_state_t>();
  loop.run_in_loop([state, factory = std::move(factory)]() mutable -> vio::task_t<void> { return run_task_coro(state, std::move(factory)); });
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
} // namespace

// ---------------- io_manager round-trips ----------------

static void io_manager_round_trip(io_manager_t &io, vio::event_loop_t &loop)
{
  auto data = pattern(200, 3);
  auto err = run_task(loop, [&]() { return io.write_object("obj_a", make_bytes(data), data.size()); });
  REQUIRE(err.code == 0);

  object_info_t info;
  err = run_task(loop, [&]() { return io.object_info("obj_a", info); });
  REQUIRE(err.code == 0);
  REQUIRE(info.exists);
  REQUIRE(info.size == data.size());

  // Whole-object read.
  std::vector<uint8_t> got(data.size());
  uint32_t br = 0;
  err = run_task(loop, [&]() { return io.read_object("obj_a", got.data(), io_range_t{0, int64_t(data.size())}, br); });
  REQUIRE(err.code == 0);
  REQUIRE(br == data.size());
  REQUIRE(memcmp(got.data(), data.data(), data.size()) == 0);

  // Range read (bytes [50, 90)).
  std::vector<uint8_t> range(40);
  err = run_task(loop, [&]() { return io.read_object("obj_a", range.data(), io_range_t{50, 40}, br); });
  REQUIRE(err.code == 0);
  REQUIRE(br == 40);
  REQUIRE(memcmp(range.data(), data.data() + 50, 40) == 0);

  // Missing object -> error + info reports not-exists.
  object_info_t missing;
  err = run_task(loop, [&]() { return io.object_info("nope", missing); });
  REQUIRE(err.code == 0);
  REQUIRE(!missing.exists);
  uint8_t dummy = 0;
  err = run_task(loop, [&]() { return io.read_object("nope", &dummy, io_range_t{0, 1}, br); });
  REQUIRE(err.code != 0);

  // Overwrite with different content/size.
  auto data2 = pattern(64, 200);
  err = run_task(loop, [&]() { return io.write_object("obj_a", make_bytes(data2), data2.size()); });
  REQUIRE(err.code == 0);
  std::vector<uint8_t> got2(data2.size());
  err = run_task(loop, [&]() { return io.read_object("obj_a", got2.data(), io_range_t{0, int64_t(data2.size())}, br); });
  REQUIRE(err.code == 0);
  REQUIRE(memcmp(got2.data(), data2.data(), data2.size()) == 0);

  // Remove is idempotent; object then reports not-exists.
  err = run_task(loop, [&]() { return io.remove_object("obj_a"); });
  REQUIRE(err.code == 0);
  err = run_task(loop, [&]() { return io.remove_object("obj_a"); });
  REQUIRE(err.code == 0);
  err = run_task(loop, [&]() { return io.object_info("obj_a", info); });
  REQUIRE(err.code == 0);
  REQUIRE(!info.exists);
}

TEST_CASE("memory_io_manager round trip")
{
  vio::thread_with_event_loop_t loop_thread;
  memory_io_manager_t io;
  io_manager_round_trip(io, loop_thread.event_loop());
}

TEST_CASE("file_dir_io_manager round trip")
{
  vio::thread_with_event_loop_t loop_thread;
  file_dir_io_manager_t io("test_io_dir", loop_thread.event_loop());
  io_manager_round_trip(io, loop_thread.event_loop());
}

// ---------------- storage_backend blob + checkpoint round-trips ----------------

// Writes two data blobs + a "tree registry" blob, checkpoints with known metadata payloads, then
// reopens a fresh backend on the same URL and verifies every blob and metadata buffer reads back
// identically. Returns the read-back attribute-config bytes so callers can compare across backends.
static std::vector<uint8_t> backend_write_and_reopen(const std::string &url, vio::event_loop_t &loop)
{
  auto blob0 = pattern(300, 11);
  auto blob1 = pattern(128, 99);
  auto registry = pattern(64, 40);
  auto attrs = pattern(50, 5);
  auto stats = pattern(37, 70);
  auto perf = pattern(80, 123);

  storage_location_t loc0, loc1, reg_loc;
  {
    points_error_t err;
    auto backend = create_storage_backend(url, loop, err);
    REQUIRE(err.code == 0);
    REQUIRE(backend);
    REQUIRE(backend->open_for_write(true).code == 0);

    backend->allocate_blob(uint32_t(blob0.size()), loc0);
    REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc0, make_bytes(blob0)); }).code == 0);
    backend->allocate_blob(uint32_t(blob1.size()), loc1);
    REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc1, make_bytes(blob1)); }).code == 0);
    backend->allocate_blob(uint32_t(registry.size()), reg_loc);
    REQUIRE(run_task(loop, [&]() { return backend->write_allocated(reg_loc, make_bytes(registry)); }).code == 0);

    checkpoint_t cp;
    cp.tree_registry = reg_loc;
    cp.attribute_configs = make_bytes(attrs);
    cp.attribute_configs_size = uint32_t(attrs.size());
    cp.stats = make_bytes(stats);
    cp.stats_size = uint32_t(stats.size());
    cp.perf = make_bytes(perf);
    cp.perf_size = uint32_t(perf.size());
    REQUIRE(run_task(loop, [&]() { return backend->write_index(std::move(cp)); }).code == 0);
  }

  // Reopen a fresh backend on the same URL.
  points_error_t err;
  auto backend = create_storage_backend(url, loop, err);
  REQUIRE(err.code == 0);
  REQUIRE(backend->exists());

  index_load_t load;
  REQUIRE(backend->read_index(load).code == 0);
  // Metadata buffers survive the round trip.
  REQUIRE(load.attribute_configs_size == attrs.size());
  REQUIRE(memcmp(load.attribute_configs.get(), attrs.data(), attrs.size()) == 0);
  REQUIRE(load.tree_registry_size == registry.size());
  REQUIRE(memcmp(load.tree_registry.get(), registry.data(), registry.size()) == 0);
  REQUIRE(load.stats_size == stats.size());
  REQUIRE(memcmp(load.stats.get(), stats.data(), stats.size()) == 0);
  REQUIRE(load.perf_size == perf.size());
  REQUIRE(memcmp(load.perf.get(), perf.data(), perf.size()) == 0);

  // Data blobs read back identically.
  std::vector<uint8_t> got0(blob0.size()), got1(blob1.size());
  uint32_t br = 0;
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(loc0, got0.data(), br); }).code == 0);
  REQUIRE(memcmp(got0.data(), blob0.data(), blob0.size()) == 0);
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(loc1, got1.data(), br); }).code == 0);
  REQUIRE(memcmp(got1.data(), blob1.data(), blob1.size()) == 0);

  return std::vector<uint8_t>(load.attribute_configs.get(), load.attribute_configs.get() + load.attribute_configs_size);
}

TEST_CASE("storage_backend write + reopen round trip is transparent across persistent modes")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  // Persistent backends survive a close + reopen (a fresh backend on the same URL). mem:// is
  // ephemeral by design (a new store each open), so it is exercised separately below without reopen.
  auto packed = backend_write_and_reopen("test_backend_packed.jlp", loop);
  auto dir = backend_write_and_reopen("dir://test_backend_dir", loop);

  // Cross-backend equivalence: the same sequence yields identical read-back bytes for both modes.
  REQUIRE(packed == dir);
}

TEST_CASE("mem:// object backend round trip (single session)")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  points_error_t err;
  auto backend = create_storage_backend("mem://ephemeral", loop, err);
  REQUIRE(err.code == 0);
  REQUIRE(backend);
  REQUIRE(backend->open_for_write(true).code == 0);

  auto blob = pattern(256, 17);
  auto registry = pattern(48, 60);
  auto attrs = pattern(50, 5);

  storage_location_t loc, reg_loc;
  backend->allocate_blob(uint32_t(blob.size()), loc);
  REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc, make_bytes(blob)); }).code == 0);
  backend->allocate_blob(uint32_t(registry.size()), reg_loc);
  REQUIRE(run_task(loop, [&]() { return backend->write_allocated(reg_loc, make_bytes(registry)); }).code == 0);

  checkpoint_t cp;
  cp.tree_registry = reg_loc;
  cp.attribute_configs = make_bytes(attrs);
  cp.attribute_configs_size = uint32_t(attrs.size());
  cp.stats = make_bytes(std::vector<uint8_t>{9});
  cp.stats_size = 1;
  cp.perf = make_bytes(std::vector<uint8_t>{8, 7});
  cp.perf_size = 2;
  REQUIRE(run_task(loop, [&]() { return backend->write_index(std::move(cp)); }).code == 0);

  // read_index re-reads the manifest from the (same-session) store.
  index_load_t load;
  REQUIRE(backend->read_index(load).code == 0);
  REQUIRE(load.attribute_configs_size == attrs.size());
  REQUIRE(memcmp(load.attribute_configs.get(), attrs.data(), attrs.size()) == 0);

  std::vector<uint8_t> got(blob.size());
  uint32_t br = 0;
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(loc, got.data(), br); }).code == 0);
  REQUIRE(memcmp(got.data(), blob.data(), blob.size()) == 0);
}

TEST_CASE("object backend identifies blobs by file_id AND offset (past the 4B cap)")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  points_error_t err;
  auto backend = create_storage_backend("mem://ids", loop, err);
  REQUIRE(err.code == 0);
  REQUIRE(backend->open_for_write(true).code == 0);

  // Two locations sharing file_id but differing only in offset (the ">4B blobs" regime, where the
  // id counter overflows file_id into offset) must be distinct objects. Drive them directly, since
  // allocate_blob can't reach 2^32 ids in a test.
  auto a = pattern(64, 1);
  auto b = pattern(64, 200);
  storage_location_t loc_a{7, uint32_t(a.size()), 0};
  storage_location_t loc_b{7, uint32_t(b.size()), 1}; // same file_id, offset 1 => a different blob id
  REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc_a, make_bytes(a)); }).code == 0);
  REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc_b, make_bytes(b)); }).code == 0);

  std::vector<uint8_t> got_a(a.size()), got_b(b.size());
  uint32_t br = 0;
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(loc_a, got_a.data(), br); }).code == 0);
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(loc_b, got_b.data(), br); }).code == 0);
  REQUIRE(memcmp(got_a.data(), a.data(), a.size()) == 0);
  REQUIRE(memcmp(got_b.data(), b.data(), b.size()) == 0); // loc_b not clobbered by loc_a
}

// ---------------- fault injection: a failed manifest write must not corrupt the committed dataset ----

namespace
{
class faulty_memory_io_t : public memory_io_manager_t
{
public:
  std::atomic<bool> fail_manifest{false};

  vio::task_t<points_error_t> write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size) override
  {
    if (fail_manifest.load() && name == object_backend_t::k_manifest_name)
    {
      points_error_t e;
      e.code = -1;
      e.msg = "injected manifest write failure";
      co_return e;
    }
    co_return co_await memory_io_manager_t::write_object(std::move(name), std::move(data), size);
  }
};
} // namespace

TEST_CASE("object backend: failed manifest write leaves the previous dataset intact")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  auto faulty = std::make_unique<faulty_memory_io_t>();
  auto *faulty_ptr = faulty.get();
  object_backend_t backend(std::move(faulty), loop);
  REQUIRE(backend.open_for_write(true).code == 0);

  auto attrs_v1 = pattern(40, 1);
  auto reg_v1 = pattern(32, 2);

  auto do_checkpoint = [&](const std::vector<uint8_t> &attrs, const std::vector<uint8_t> &reg) {
    storage_location_t reg_loc;
    backend.allocate_blob(uint32_t(reg.size()), reg_loc);
    REQUIRE(run_task(loop, [&]() { return backend.write_allocated(reg_loc, make_bytes(reg)); }).code == 0);
    checkpoint_t cp;
    cp.tree_registry = reg_loc;
    cp.attribute_configs = make_bytes(attrs);
    cp.attribute_configs_size = uint32_t(attrs.size());
    cp.stats = make_bytes(std::vector<uint8_t>{1, 2, 3});
    cp.stats_size = 3;
    cp.perf = make_bytes(std::vector<uint8_t>{4, 5, 6, 7});
    cp.perf_size = 4;
    return run_task(loop, [&]() { return backend.write_index(std::move(cp)); });
  };

  // Checkpoint 1 succeeds and becomes the committed dataset.
  REQUIRE(do_checkpoint(attrs_v1, reg_v1).code == 0);

  // Checkpoint 2 writes new metadata objects but the manifest write is injected to fail.
  faulty_ptr->fail_manifest.store(true);
  auto attrs_v2 = pattern(41, 200);
  auto reg_v2 = pattern(33, 201);
  REQUIRE(do_checkpoint(attrs_v2, reg_v2).code != 0);

  // The store's manifest must still point at checkpoint 1 (the ordering/crash-safety invariant).
  index_load_t load;
  REQUIRE(backend.read_index(load).code == 0);
  REQUIRE(load.attribute_configs_size == attrs_v1.size());
  REQUIRE(memcmp(load.attribute_configs.get(), attrs_v1.data(), attrs_v1.size()) == 0);
  REQUIRE(load.tree_registry_size == reg_v1.size());
  REQUIRE(memcmp(load.tree_registry.get(), reg_v1.data(), reg_v1.size()) == 0);

  // Recovery: once the fault clears, a fresh checkpoint commits normally.
  faulty_ptr->fail_manifest.store(false);
  auto attrs_v3 = pattern(45, 90);
  auto reg_v3 = pattern(48, 91);
  REQUIRE(do_checkpoint(attrs_v3, reg_v3).code == 0);
  index_load_t load3;
  REQUIRE(backend.read_index(load3).code == 0);
  REQUIRE(load3.attribute_configs_size == attrs_v3.size());
  REQUIRE(memcmp(load3.attribute_configs.get(), attrs_v3.data(), attrs_v3.size()) == 0);
}

// ---------------- the previous tree-registry blob must be reclaimed on the next checkpoint ----------

namespace
{
// One checkpoint carrying a freshly-written registry blob of `reg_size` bytes; returns its location.
storage_location_t do_registry_checkpoint(storage_backend_t &backend, vio::event_loop_t &loop, uint32_t reg_size, uint8_t seed)
{
  auto reg = pattern(reg_size, seed);
  storage_location_t reg_loc;
  backend.allocate_blob(reg_size, reg_loc);
  REQUIRE(run_task(loop, [&]() { return backend.write_allocated(reg_loc, make_bytes(reg)); }).code == 0);
  checkpoint_t cp;
  cp.tree_registry = reg_loc;
  cp.attribute_configs = make_bytes(pattern(16, 1));
  cp.attribute_configs_size = 16;
  cp.stats = make_bytes(pattern(8, 2));
  cp.stats_size = 8;
  cp.perf = make_bytes(pattern(12, 3));
  cp.perf_size = 12;
  REQUIRE(run_task(loop, [&]() { return backend.write_index(std::move(cp)); }).code == 0);
  return reg_loc;
}
} // namespace

TEST_CASE("object backend reclaims the superseded tree-registry object")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  points_error_t err;
  auto backend = create_storage_backend("mem://regfree", loop, err);
  REQUIRE(err.code == 0);
  REQUIRE(backend->open_for_write(true).code == 0);

  auto reg1 = do_registry_checkpoint(*backend, loop, 40, 7);
  std::vector<uint8_t> tmp(40);
  uint32_t br = 0;
  // After checkpoint 1 the registry object is live.
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(reg1, tmp.data(), br); }).code == 0);

  auto reg2 = do_registry_checkpoint(*backend, loop, 44, 9);
  // After checkpoint 2 the previous registry object is gone (previously it leaked forever).
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(reg1, tmp.data(), br); }).code != 0);
  // The current registry object is still readable.
  std::vector<uint8_t> tmp2(44);
  REQUIRE(run_task(loop, [&]() { return backend->read_blob(reg2, tmp2.data(), br); }).code == 0);
}

TEST_CASE("packed backend does not leak registry blobs across checkpoints")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();

  const char *path = "test_leak_packed.jlp";
  std::remove(path);

  points_error_t err;
  auto backend = create_storage_backend(path, loop, err);
  REQUIRE(err.code == 0);
  REQUIRE(backend->open_for_write(true).code == 0);

  // Large registry blobs so a per-checkpoint leak would balloon the file well past the steady state.
  const uint32_t reg_size = 4096;
  for (int i = 0; i < 20; i++)
    do_registry_checkpoint(*backend, loop, reg_size, uint8_t(i));

  // With the leak, the file would grow ~20 * 4096 = 80 KB of orphaned registry blobs. The fix reuses
  // the freed space, so the file stays a small steady-state size.
  std::error_code ec;
  auto size = std::filesystem::file_size(path, ec);
  REQUIRE(!ec);
  REQUIRE(size < 30000);

  std::remove(path);
}
