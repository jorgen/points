#include <doctest/doctest.h>

#include <object_backend.hpp>
#include <packed_file_backend.hpp>
#include <storage_backend.hpp>
#include <index_format.hpp>
#include <conversion_types.hpp>

#include <vio/objstore/memory_object_store.h>

#include <vio/event_loop.h>
#include <vio/task.h>

#include <atomic>
#include <cstdio>
#include <expected>
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

// The io_manager backends themselves (memory / file-dir / cloud) now live in vio and are unit-tested
// there (vio/test/test_objstore.cpp). These tests cover the points storage backends built on top.

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

    backend->allocate_blob(uint32_t(blob0.size()), points::converter::storage_backend_t::blob_kind_t::data, loc0);
    REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc0, make_bytes(blob0)); }).code == 0);
    backend->allocate_blob(uint32_t(blob1.size()), points::converter::storage_backend_t::blob_kind_t::data, loc1);
    REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc1, make_bytes(blob1)); }).code == 0);
    backend->allocate_blob(uint32_t(registry.size()), points::converter::storage_backend_t::blob_kind_t::metadata, reg_loc);
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
  backend->allocate_blob(uint32_t(blob.size()), points::converter::storage_backend_t::blob_kind_t::data, loc);
  REQUIRE(run_task(loop, [&]() { return backend->write_allocated(loc, make_bytes(blob)); }).code == 0);
  backend->allocate_blob(uint32_t(registry.size()), points::converter::storage_backend_t::blob_kind_t::metadata, reg_loc);
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
class faulty_memory_io_t : public vio::objstore::memory_io_manager_t
{
public:
  std::atomic<bool> fail_manifest{false};

  vio::task_t<std::expected<void, vio::error_t>> write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size) override
  {
    if (fail_manifest.load() && name == object_backend_t::k_manifest_name)
      co_return std::unexpected(vio::error_t{.code = -1, .msg = "injected manifest write failure"});
    co_return co_await vio::objstore::memory_io_manager_t::write_object(std::move(name), std::move(data), size);
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
    backend.allocate_blob(uint32_t(reg.size()), points::converter::storage_backend_t::blob_kind_t::metadata, reg_loc);
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
  backend.allocate_blob(reg_size, points::converter::storage_backend_t::blob_kind_t::metadata, reg_loc);
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

TEST_CASE("index extras round-trip and legacy zeros decode as absent")
{
  using namespace points::converter;
  // With extras: everything must survive the 128-byte block.
  index_extras_t extras;
  extras.residency_table = {0, 512, 8192};
  for (int i = 0; i < 16; i++)
    extras.dataset_uuid[i] = uint8_t(i + 1);
  extras.version_flags = 0x0102;
  storage_location_t free_blobs = {0, 10, 100}, attrs = {0, 20, 200}, registry = {0, 30, 300}, stats = {0, 40, 400}, perf = {0, 50, 500};
  auto blob = serialize_index(k_serialized_index_size, free_blobs, attrs, registry, stats, perf, &extras);

  storage_location_t r_free = {}, r_attrs = {}, r_reg = {}, r_stats = {}, r_perf = {};
  index_extras_t r_extras;
  auto err = deserialize_index(blob.get(), k_serialized_index_size, r_free, r_attrs, r_reg, r_stats, r_perf, &r_extras);
  REQUIRE(err.code == 0);
  REQUIRE(r_reg.offset == 300);
  REQUIRE(r_extras.residency_table.offset == 8192);
  REQUIRE(r_extras.residency_table.size == 512);
  REQUIRE(memcmp(r_extras.dataset_uuid, extras.dataset_uuid, 16) == 0);
  REQUIRE(r_extras.version_flags == 0x0102);

  // Legacy writer (no extras): the spare bytes are zero and must decode as absent.
  auto legacy = serialize_index(k_serialized_index_size, free_blobs, attrs, registry, stats, perf);
  index_extras_t l_extras;
  err = deserialize_index(legacy.get(), k_serialized_index_size, r_free, r_attrs, r_reg, r_stats, r_perf, &l_extras);
  REQUIRE(err.code == 0);
  REQUIRE(l_extras.residency_table.size == 0);
  REQUIRE(l_extras.residency_table.offset == 0);
  uint8_t zero_uuid[16] = {};
  REQUIRE(memcmp(l_extras.dataset_uuid, zero_uuid, 16) == 0);
  REQUIRE(l_extras.version_flags == 0);
}

// ---------------- cache tier: residency persists across checkpoints and reopens ----------------

TEST_CASE("packed cache tier: residency round-trips through checkpoint + reopen")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();
  const char *path = "test_cache_tier_packed.jlp";
  std::remove(path);

  using packed_t = points::converter::packed_file_backend_t;
  storage_location_t blob_a = {}, blob_b = {};
  uint8_t uuid[16];
  for (int i = 0; i < 16; i++)
    uuid[i] = uint8_t(0xA0 + i);

  {
    points_error_t err;
    packed_t backend(path, loop, err);
    REQUIRE(err.code == 0);
    backend.enable_cache_tier(/*cap=*/0);
    backend.set_dataset_uuid(uuid);
    REQUIRE(backend.open_for_write(true).code == 0);

    auto a = pattern(4096, 11);
    auto b = pattern(4096, 22);
    backend.allocate_blob(uint32_t(a.size()), points::converter::storage_backend_t::blob_kind_t::data, blob_a);
    backend.allocate_blob(uint32_t(b.size()), points::converter::storage_backend_t::blob_kind_t::data, blob_b);
    REQUIRE(run_task(loop, [&]() { return backend.write_allocated(blob_a, make_bytes(a)); }).code == 0);
    REQUIRE(run_task(loop, [&]() { return backend.write_allocated(blob_b, make_bytes(b)); }).code == 0);
    REQUIRE(backend.residency()->resident_bytes() >= 8192);

    // blob_a acquires a remote copy; not evictable until a checkpoint commits the fact.
    backend.residency()->mark_uploaded(blob_a.offset, blob_a.size, /*remote_id=*/42);
    REQUIRE(backend.residency()->pick_evict_victim() == nullptr);
    // Orderly teardown: this checkpoint is the clean one.
    backend.set_clean_shutdown_next_checkpoint();
    do_registry_checkpoint(backend, loop, 64, 5);
    auto *victim = backend.residency()->pick_evict_victim();
    REQUIRE(victim != nullptr);
    REQUIRE(victim->offset == blob_a.offset);
    REQUIRE(victim->remote_id == 42);
  }

  // Reopen WITHOUT pre-enabling the tier: the superblock extras restore it, clean shutdown keeps
  // the local_uploaded state (no demotion), and the uuid survives.
  {
    points_error_t err;
    packed_t backend(path, loop, err);
    REQUIRE(err.code == 0);
    REQUIRE(backend.exists());
    index_load_t load;
    REQUIRE(backend.read_index(load).code == 0);
    REQUIRE(backend.restore_allocator(load.free_blobs, load.free_blobs_size).code == 0);
    REQUIRE(backend.open_for_write(false).code == 0);
    REQUIRE(backend.cache_tier_enabled());
    REQUIRE(memcmp(backend.dataset_uuid(), uuid, 16) == 0);
    auto *entry = backend.residency()->find(blob_a.offset);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->state == points::converter::blob_residency_state_t::local_uploaded);
    REQUIRE(entry->durable); // persisted facts are durable by construction
    REQUIRE(backend.residency()->find(blob_b.offset) == nullptr); // plain LOCAL: no entry

    // Mid-session (unclean) checkpoint, then reopen: local_uploaded demotes to remote_uploaded.
    do_registry_checkpoint(backend, loop, 64, 6);
  }
  {
    points_error_t err;
    packed_t backend(path, loop, err);
    REQUIRE(err.code == 0);
    index_load_t load;
    REQUIRE(backend.read_index(load).code == 0);
    REQUIRE(backend.restore_allocator(load.free_blobs, load.free_blobs_size).code == 0);
    REQUIRE(backend.open_for_write(false).code == 0);
    REQUIRE(backend.cache_tier_enabled());
    auto *entry = backend.residency()->find(blob_a.offset);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->state == points::converter::blob_residency_state_t::remote_uploaded);
  }

  std::remove(path);
}

TEST_CASE("packed classic mode: no residency table, extras stay zero")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();
  const char *path = "test_classic_packed.jlp";
  std::remove(path);
  {
    points_error_t err;
    points::converter::packed_file_backend_t backend(path, loop, err);
    REQUIRE(err.code == 0);
    REQUIRE(backend.open_for_write(true).code == 0);
    do_registry_checkpoint(backend, loop, 64, 9);
  }
  {
    points_error_t err;
    points::converter::packed_file_backend_t backend(path, loop, err);
    REQUIRE(err.code == 0);
    index_load_t load;
    REQUIRE(backend.read_index(load).code == 0);
    REQUIRE(backend.restore_allocator(load.free_blobs, load.free_blobs_size).code == 0);
    REQUIRE(backend.open_for_write(false).code == 0);
    REQUIRE(!backend.cache_tier_enabled()); // zero extras -> tier stays dormant
  }
  std::remove(path);
}

// ---------------- cache tier: spill + eviction under a hard cap ----------------

namespace
{
// Forwards to a shared memory store so two backend "sessions" can see the same bucket (mem:// gives
// each open a fresh store; crash/reopen tests need persistence).
class shared_memory_io_t : public vio::objstore::io_manager_t
{
public:
  explicit shared_memory_io_t(std::shared_ptr<vio::objstore::memory_io_manager_t> inner)
    : _inner(std::move(inner))
  {
  }
  vio::task_t<std::expected<uint64_t, vio::error_t>> read_object(std::string name, uint8_t *dst, vio::objstore::io_range_t range) override
  {
    return _inner->read_object(std::move(name), dst, range);
  }
  vio::task_t<std::expected<void, vio::error_t>> write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size) override
  {
    return _inner->write_object(std::move(name), std::move(data), size);
  }
  vio::task_t<std::expected<vio::objstore::object_info_t, vio::error_t>> object_info(std::string name) override
  {
    return _inner->object_info(std::move(name));
  }
  vio::task_t<std::expected<void, vio::error_t>> remove_object(std::string name) override
  {
    return _inner->remove_object(std::move(name));
  }

private:
  std::shared_ptr<vio::objstore::memory_io_manager_t> _inner;
};
} // namespace

TEST_CASE("packed cache tier: hard cap diverts writes to spill, reads stay transparent")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();
  const char *path = "test_spill_packed.jlp";
  std::remove(path);
  auto bucket = std::make_shared<vio::objstore::memory_io_manager_t>();

  auto a = pattern(10240, 1);
  auto b = pattern(10240, 2);
  storage_location_t loc_a = {}, loc_b = {};
  {
    points_error_t err;
    points::converter::packed_file_backend_t backend(path, loop, err);
    REQUIRE(err.code == 0);
    backend.enable_cache_tier(/*cap=*/16 * 1024);
    backend.enable_spill(std::make_unique<shared_memory_io_t>(bucket), "spill/", /*segment target*/ 32 * 1024);
    REQUIRE(backend.open_for_write(true).code == 0);

    backend.allocate_blob(uint32_t(a.size()), points::converter::storage_backend_t::blob_kind_t::data, loc_a);
    REQUIRE(run_task(loop, [&]() { return backend.write_allocated(loc_a, make_bytes(a)); }).code == 0);
    // Second data blob exceeds the hard cap -> born remote_spilled, resident bytes unchanged.
    backend.allocate_blob(uint32_t(b.size()), points::converter::storage_backend_t::blob_kind_t::data, loc_b);
    REQUIRE(run_task(loop, [&]() { return backend.write_allocated(loc_b, make_bytes(b)); }).code == 0);
    auto *entry_b = backend.residency()->find(loc_b.offset);
    REQUIRE(entry_b != nullptr);
    REQUIRE(entry_b->state == points::converter::blob_residency_state_t::remote_spilled);
    REQUIRE(backend.residency()->resident_bytes() < 16 * 1024);

    // Reads are transparent for both: local pread for a, buffered/remote spill read for b.
    std::vector<uint8_t> got_a(a.size()), got_b(b.size());
    uint32_t br = 0;
    REQUIRE(run_task(loop, [&]() { return backend.read_blob(loc_a, got_a.data(), br); }).code == 0);
    REQUIRE(memcmp(got_a.data(), a.data(), a.size()) == 0);
    REQUIRE(run_task(loop, [&]() { return backend.read_blob(loc_b, got_b.data(), br); }).code == 0);
    REQUIRE(memcmp(got_b.data(), b.data(), b.size()) == 0);

    // Checkpoint flushes the open segment; the persisted table references it; reopen reads it back.
    do_registry_checkpoint(backend, loop, 64, 3);
    std::fill(got_b.begin(), got_b.end(), 0);
    REQUIRE(run_task(loop, [&]() { return backend.read_blob(loc_b, got_b.data(), br); }).code == 0);
    REQUIRE(memcmp(got_b.data(), b.data(), b.size()) == 0);
  }

  // Reopen: spilled blob still readable through the restored table + segment object.
  {
    points_error_t err;
    points::converter::packed_file_backend_t backend(path, loop, err);
    REQUIRE(err.code == 0);
    backend.enable_cache_tier(16 * 1024);
    backend.enable_spill(std::make_unique<shared_memory_io_t>(bucket), "spill/", 32 * 1024);
    index_load_t load;
    REQUIRE(backend.read_index(load).code == 0);
    REQUIRE(backend.restore_allocator(load.free_blobs, load.free_blobs_size).code == 0);
    REQUIRE(backend.open_for_write(false).code == 0);
    REQUIRE(run_task(loop, [&]() { return backend.spill_bootstrap(); }).code == 0);
    std::vector<uint8_t> got_b(b.size());
    uint32_t br = 0;
    REQUIRE(run_task(loop, [&]() { return backend.read_blob(loc_b, got_b.data(), br); }).code == 0);
    REQUIRE(memcmp(got_b.data(), b.data(), b.size()) == 0);
  }
  std::remove(path);
}

TEST_CASE("packed cache tier: eviction punches uploaded blobs after their checkpoint")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();
  const char *path = "test_evict_packed.jlp";
  std::remove(path);

  points_error_t err;
  points::converter::packed_file_backend_t backend(path, loop, err);
  REQUIRE(err.code == 0);
  // Cap sized so two 10KB blobs (20480 resident) sit ABOVE the soft watermark (cap - cap/8 = 19712)
  // without tripping the hard cap at allocation -> pressure exists, eviction runs post-checkpoint.
  backend.enable_cache_tier(22 * 1024);
  REQUIRE(backend.open_for_write(true).code == 0);

  auto a = pattern(10240, 7);
  auto b = pattern(10240, 8);
  storage_location_t loc_a = {}, loc_b = {};
  backend.allocate_blob(uint32_t(a.size()), points::converter::storage_backend_t::blob_kind_t::data, loc_a);
  REQUIRE(run_task(loop, [&]() { return backend.write_allocated(loc_a, make_bytes(a)); }).code == 0);
  backend.allocate_blob(uint32_t(b.size()), points::converter::storage_backend_t::blob_kind_t::data, loc_b);
  REQUIRE(run_task(loop, [&]() { return backend.write_allocated(loc_b, make_bytes(b)); }).code == 0);

  // Both uploaded (e.g. their band committed); facts become durable at the next checkpoint.
  backend.residency()->mark_uploaded(loc_a.offset, loc_a.size, 100);
  backend.residency()->mark_uploaded(loc_b.offset, loc_b.size, 101);
  REQUIRE(backend.wants_checkpoint()); // over soft, nothing durable yet
  const uint64_t resident_before = backend.residency()->resident_bytes();
  do_registry_checkpoint(backend, loop, 64, 4); // post-commit: durable flip + eviction pass

  // The pass punched at least one victim (LRU order: a first).
  auto *entry_a = backend.residency()->find(loc_a.offset);
  REQUIRE(entry_a != nullptr);
  const bool punch_worked = entry_a->state == points::converter::blob_residency_state_t::remote_uploaded;
  if (punch_worked)
  {
    REQUIRE(backend.residency()->resident_bytes() < resident_before);
    // Reading an evicted blob without a destination resolver is a clear error, not garbage.
    std::vector<uint8_t> got(a.size());
    uint32_t br = 0;
    REQUIRE(run_task(loop, [&]() { return backend.read_blob(loc_a, got.data(), br); }).code != 0);
  }
  else
  {
    WARN("hole punch unsupported on this filesystem -- eviction degraded to mark-only");
  }
  std::remove(path);
}

TEST_CASE("packed cache tier: spill-existing pass moves local blobs out and punches after checkpoint")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();
  const char *path = "test_spill_existing_packed.jlp";
  std::remove(path);
  auto bucket = std::make_shared<vio::objstore::memory_io_manager_t>();

  points_error_t err;
  points::converter::packed_file_backend_t backend(path, loop, err);
  REQUIRE(err.code == 0);
  // Cap: 3 x 10KB blobs = 30720 resident, over soft (32768 * 7/8 = 28672) while every allocation
  // stays under the hard cap (max prefix 30720 <= 32768) so all three land LOCAL.
  backend.enable_cache_tier(32 * 1024);
  backend.enable_spill(std::make_unique<shared_memory_io_t>(bucket), "spill/", 64 * 1024);
  REQUIRE(backend.open_for_write(true).code == 0);

  auto a = pattern(10240, 31);
  auto b = pattern(10240, 32);
  auto c = pattern(10240, 33);
  storage_location_t loc_a = {}, loc_b = {}, loc_c = {};
  for (auto [buf, loc] : {std::pair{&a, &loc_a}, std::pair{&b, &loc_b}, std::pair{&c, &loc_c}})
  {
    backend.allocate_blob(uint32_t(buf->size()), points::converter::storage_backend_t::blob_kind_t::data, *loc);
    REQUIRE(run_task(loop, [&]() { return backend.write_allocated(*loc, make_bytes(*buf)); }).code == 0);
  }

  // Nothing uploaded -> eviction has no victims; the spill-existing pass must relieve pressure,
  // taking the HIGHEST offsets first (needed latest by LOD).
  REQUIRE(run_task(loop, [&]() { return backend.run_spill_pass(); }).code == 0);
  auto *entry_c = backend.residency()->find(loc_c.offset);
  REQUIRE(entry_c != nullptr);
  REQUIRE(entry_c->state == points::converter::blob_residency_state_t::local_spilled);
  REQUIRE(backend.residency()->find(loc_a.offset) == nullptr); // lowest offset spared

  // Still readable from local bytes while the spill fact is pending.
  std::vector<uint8_t> got(c.size());
  uint32_t br = 0;
  REQUIRE(run_task(loop, [&]() { return backend.read_blob(loc_c, got.data(), br); }).code == 0);
  REQUIRE(memcmp(got.data(), c.data(), c.size()) == 0);

  // Checkpoint: fact turns durable, post-commit punch drops the local bytes (if punch works here).
  const uint64_t resident_before = backend.residency()->resident_bytes();
  do_registry_checkpoint(backend, loop, 64, 12);
  entry_c = backend.residency()->find(loc_c.offset);
  REQUIRE(entry_c != nullptr);
  if (entry_c->state == points::converter::blob_residency_state_t::remote_spilled)
  {
    REQUIRE(backend.residency()->resident_bytes() < resident_before);
    // And the read now comes from the spill segment.
    std::fill(got.begin(), got.end(), 0);
    REQUIRE(run_task(loop, [&]() { return backend.read_blob(loc_c, got.data(), br); }).code == 0);
    REQUIRE(memcmp(got.data(), c.data(), c.size()) == 0);
  }
  else
  {
    WARN("hole punch unsupported -- spilled blob kept local (degraded mode)");
  }
  std::remove(path);
}

TEST_CASE("packed cache tier: freeing tracked blobs removes entries, derefs segments, never recycles offsets")
{
  vio::thread_with_event_loop_t loop_thread;
  auto &loop = loop_thread.event_loop();
  const char *path = "test_freed_tracked.jlp";
  std::remove(path);
  auto bucket = std::make_shared<vio::objstore::memory_io_manager_t>();

  points_error_t err;
  points::converter::packed_file_backend_t backend(path, loop, err);
  REQUIRE(err.code == 0);
  backend.enable_cache_tier(/*cap=*/16 * 1024);
  backend.enable_spill(std::make_unique<shared_memory_io_t>(bucket), "spill/", /*segment target*/ 32 * 1024);
  REQUIRE(backend.open_for_write(true).code == 0);

  auto a = pattern(10240, 1);
  auto b = pattern(10240, 2);
  storage_location_t loc_a = {}, loc_b = {};
  backend.allocate_blob(uint32_t(a.size()), points::converter::storage_backend_t::blob_kind_t::data, loc_a);
  REQUIRE(run_task(loop, [&]() { return backend.write_allocated(loc_a, make_bytes(a)); }).code == 0);
  // b exceeds the hard cap -> born remote_spilled into segment 0.
  backend.allocate_blob(uint32_t(b.size()), points::converter::storage_backend_t::blob_kind_t::data, loc_b);
  REQUIRE(run_task(loop, [&]() { return backend.write_allocated(loc_b, make_bytes(b)); }).code == 0);
  // a gets uploaded -> local_uploaded (tracked with local bytes).
  backend.note_blob_uploaded(loc_a.offset, loc_a.size, /*remote_id*/ 0x1234);
  REQUIRE(backend.residency()->find(loc_a.offset) != nullptr);
  REQUIRE(backend.residency()->find(loc_b.offset) != nullptr);

  // Checkpoint 1: facts durable, spill segment flushed + journaled.
  do_registry_checkpoint(backend, loop, 64, 3);
  shared_memory_io_t probe(bucket);
  auto segment_exists = [&](bool &exists) {
    return run_task(loop, [&]() -> vio::task_t<points_error_t> {
      auto info = co_await probe.object_info("spill/seg_00000001");
      exists = info.has_value() && info->exists;
      co_return points_error_t{};
    });
  };
  bool seg_exists = false;
  REQUIRE(segment_exists(seg_exists).code == 0);
  REQUIRE(seg_exists);

  // Checkpoint 2 frees BOTH tracked blobs (superseded-LOD style).
  {
    auto reg = pattern(64, 4);
    storage_location_t reg_loc;
    backend.allocate_blob(64, points::converter::storage_backend_t::blob_kind_t::metadata, reg_loc);
    REQUIRE(run_task(loop, [&]() { return backend.write_allocated(reg_loc, make_bytes(reg)); }).code == 0);
    checkpoint_t cp;
    cp.tree_registry = reg_loc;
    cp.attribute_configs = make_bytes(pattern(16, 1));
    cp.attribute_configs_size = 16;
    cp.stats = make_bytes(pattern(8, 2));
    cp.stats_size = 8;
    cp.perf = make_bytes(pattern(12, 3));
    cp.perf_size = 12;
    cp.freed.push_back(loc_a);
    cp.freed.push_back(loc_b);
    REQUIRE(run_task(loop, [&]() { return backend.write_index(std::move(cp)); }).code == 0);
  }

  // Entries gone; the orphaned spill segment was swept by the same checkpoint.
  REQUIRE(backend.residency()->find(loc_a.offset) == nullptr);
  REQUIRE(backend.residency()->find(loc_b.offset) == nullptr);
  REQUIRE(segment_exists(seg_exists).code == 0);
  REQUIRE(!seg_exists);

  // Freed-while-tracked offsets never return to the allocator: fresh allocations of the same sizes
  // land elsewhere (a recycled offset would alias the stale remote copy through the old table).
  storage_location_t loc_c = {}, loc_d = {};
  backend.allocate_blob(uint32_t(a.size()), points::converter::storage_backend_t::blob_kind_t::data, loc_c);
  backend.allocate_blob(uint32_t(b.size()), points::converter::storage_backend_t::blob_kind_t::data, loc_d);
  REQUIRE(loc_c.offset != loc_a.offset);
  REQUIRE(loc_c.offset != loc_b.offset);
  REQUIRE(loc_d.offset != loc_a.offset);
  REQUIRE(loc_d.offset != loc_b.offset);

  std::remove(path);
}
