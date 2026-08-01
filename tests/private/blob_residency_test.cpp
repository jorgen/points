#include <doctest/doctest.h>

#include <blob_residency.hpp>
#include <file_hole_punch.hpp>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
using namespace dew::converter;

TEST_CASE("residency: local blobs have no entries and accounting tracks resident bytes")
{
  blob_residency_t r;
  r.set_cap(1000);
  r.account_alloc(500);
  REQUIRE(r.resident_bytes() == 500);
  REQUIRE(!r.over_soft());
  REQUIRE(!r.over_hard(0));
  r.account_alloc(400);
  REQUIRE(r.over_soft());       // 900 > 875
  REQUIRE(r.over_hard(200));    // 900 + 200 > 1000
  REQUIRE(!r.over_hard(50));
  REQUIRE(r.find(4096) == nullptr);
  REQUIRE(r.entry_count() == 0);
}

TEST_CASE("residency: durable gating -- evict refused until the checkpoint commits")
{
  blob_residency_t r;
  r.set_cap(1000);
  r.account_alloc(100);
  r.mark_uploaded(4096, 100, /*remote_id=*/1);

  // Not durable yet: no victim, evict refused.
  REQUIRE(r.pick_evict_victim() == nullptr);
  REQUIRE(!r.evict(4096));

  // Checkpoint: snapshot generation, then commit.
  auto gen = r.serialize_generation();
  r.commit_durable(gen);
  auto *victim = r.pick_evict_victim();
  REQUIRE(victim != nullptr);
  REQUIRE(victim->offset == 4096);
  REQUIRE(r.evict(4096));
  REQUIRE(r.resident_bytes() == 0);
  REQUIRE(r.find(4096)->state == blob_residency_state_t::remote_uploaded);
  // Second evict is a no-op refusal.
  REQUIRE(!r.evict(4096));
}

TEST_CASE("residency: facts recorded after the snapshot stay pending")
{
  blob_residency_t r;
  r.account_alloc(200);
  r.mark_uploaded(0, 100, 1);
  auto gen = r.serialize_generation(); // checkpoint snapshot taken here
  r.mark_uploaded(4096, 100, 2);       // recorded AFTER the snapshot
  r.commit_durable(gen);
  REQUIRE(r.evict(0));      // in the committed snapshot -> durable
  REQUIRE(!r.evict(4096));  // not in it -> still pending
  r.commit_durable(r.serialize_generation());
  REQUIRE(r.evict(4096));
}

TEST_CASE("residency: LRU order and in-flight reads")
{
  blob_residency_t r;
  r.account_alloc(300);
  r.mark_uploaded(0, 100, 1);
  r.mark_uploaded(4096, 100, 2);
  r.mark_uploaded(8192, 100, 3);
  r.commit_durable(r.serialize_generation());

  // LRU tail is the first-marked entry (offset 0).
  REQUIRE(r.pick_evict_victim()->offset == 0);
  // Touch it -> tail moves to offset 4096.
  r.touch(*r.find(0));
  REQUIRE(r.pick_evict_victim()->offset == 4096);
  // An in-flight local read protects the tail; the scan walks to the next candidate.
  r.begin_local_read(*r.find(4096));
  REQUIRE(r.pick_evict_victim()->offset == 8192);
  r.end_local_read(*r.find(4096));
  REQUIRE(r.pick_evict_victim()->offset == 4096);
}

TEST_CASE("residency: spill transitions and uploader takeover")
{
  blob_residency_t r;
  r.account_alloc(100);
  r.mark_spilled(4096, 100, /*segment remote_id=*/0x100000042u);
  REQUIRE(r.find(4096)->state == blob_residency_state_t::local_spilled);
  // Not durable: local bytes may not be dropped yet.
  REQUIRE(!r.drop_spilled_local(4096));
  r.commit_durable(r.serialize_generation());
  REQUIRE(r.drop_spilled_local(4096));
  REQUIRE(r.find(4096)->state == blob_residency_state_t::remote_spilled);
  REQUIRE(r.resident_bytes() == 0);

  // The uploader ships the spilled blob to the final layout: state flips to remote_uploaded and
  // the old spill locator is handed back for segment refcounting.
  auto old_spill = r.mark_uploaded(4096, 100, /*pack remote_id=*/7);
  REQUIRE(old_spill == 0x100000042u);
  REQUIRE(r.find(4096)->state == blob_residency_state_t::remote_uploaded);

  // Direct spill: born remote, resident bytes never charged.
  r.mark_spilled_remote(16384, 50, 0x200000000u);
  REQUIRE(r.find(16384)->state == blob_residency_state_t::remote_spilled);
  REQUIRE(r.resident_bytes() == 0);
}

TEST_CASE("residency: serialize round-trip, clean and unclean shutdown")
{
  blob_residency_t r;
  r.set_cap(1 << 20);
  r.account_alloc(300);
  r.mark_uploaded(0, 100, 1);
  r.mark_spilled(4096, 100, 2);
  r.mark_spilled_remote(8192, 100, 3);
  r.commit_durable(r.serialize_generation());
  r.evict(0); // -> remote_uploaded, resident 300-100 = 200

  // Clean shutdown: states restore verbatim.
  auto clean = r.serialize(true);
  REQUIRE(!clean.empty());
  blob_residency_t restored;
  REQUIRE(restored.deserialize(clean.data(), uint32_t(clean.size())).code == 0);
  REQUIRE(restored.cap() == r.cap());
  REQUIRE(restored.resident_bytes() == r.resident_bytes());
  REQUIRE(restored.find(0)->state == blob_residency_state_t::remote_uploaded);
  REQUIRE(restored.find(4096)->state == blob_residency_state_t::local_spilled);
  REQUIRE(restored.find(8192)->state == blob_residency_state_t::remote_spilled);
  // Everything persisted is durable on reopen (it was in a committed checkpoint by construction).
  REQUIRE(restored.drop_spilled_local(4096));

  // Unclean shutdown: local_* entries demote to remote_* (a punch may have raced the crash).
  auto unclean = r.serialize(false);
  blob_residency_t demoted;
  REQUIRE(demoted.deserialize(unclean.data(), uint32_t(unclean.size())).code == 0);
  REQUIRE(demoted.find(4096)->state == blob_residency_state_t::remote_spilled);
  REQUIRE(demoted.resident_bytes() == r.resident_bytes() - 100); // the demoted local bytes
}

#ifndef _WIN32
TEST_CASE("hole punch reclaims blocks or reports unsupported")
{
  char path[] = "/tmp/dew_hole_punch_test_XXXXXX";
  int fd = mkstemp(path);
  REQUIRE(fd >= 0);

  // 1 MiB of non-zero data so blocks are genuinely allocated.
  std::vector<uint8_t> chunk(1 << 20, 0xAB);
  REQUIRE(write(fd, chunk.data(), chunk.size()) == ssize_t(chunk.size()));
  REQUIRE(fsync(fd) == 0);

  struct stat before = {};
  REQUIRE(fstat(fd, &before) == 0);

  auto result = dew::converter::file_hole_punch(fd, 4096, (1 << 20) - 8192);
  if (result.status == dew::converter::hole_punch_status_t::unsupported)
  {
    // Legit on exotic filesystems: the backend degrades to mark-only eviction.
    WARN("hole punch unsupported on this filesystem -- degraded mode only");
  }
  else
  {
    REQUIRE(result.status == dew::converter::hole_punch_status_t::ok);
    struct stat after = {};
    REQUIRE(fstat(fd, &after) == 0);
    REQUIRE(after.st_size == before.st_size);     // logical size unchanged (KEEP_SIZE semantics)
    REQUIRE(after.st_blocks < before.st_blocks);  // physical blocks reclaimed
  }
  close(fd);
  std::remove(path);
}
#endif

} // namespace
