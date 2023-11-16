#include "blob_manager.hpp" // Replace with the correct path to your header
#include <catch2/catch.hpp>

TEST_CASE("Blob registration", "[blob_manager_t]")
{
  blob_manager_t manager;

  SECTION("Register a single blob")
  {
    auto offset = manager.register_blob({10});
    REQUIRE(offset.data == 0);
  }

  SECTION("Register multiple blobs")
  {
    auto offset1 = manager.register_blob({10});
    auto offset2 = manager.register_blob({20});
    REQUIRE(offset1.data == 0);
    REQUIRE(offset2.data == 10);
  }
}

TEST_CASE("Blob unregistration and reuse of space", "[blob_manager_t]")
{
  blob_manager_t manager;
  auto offset1 = manager.register_blob({10});
  auto offset2 = manager.register_blob({20});

  SECTION("Unregister a blob and check free space")
  {
    REQUIRE(manager.unregister_blob(offset1, {10}) == true);
    auto offset3 = manager.register_blob({5});
    REQUIRE(offset3.data == 0); // Reuse the free space
  }

  SECTION("Unregister multiple blobs and check merging of free space")
  {
    REQUIRE(manager.unregister_blob(offset1, {10}) == true);
    REQUIRE(manager.unregister_blob(offset2, {20}) == true);
    auto offset3 = manager.register_blob({25});
    REQUIRE(offset3.data == 0); // Merged free space should allow a blob of size 25
  }
}

TEST_CASE("Registering blobs with no available space", "[blob_manager_t]")
{
  blob_manager_t manager;

  SECTION("Register a blob larger than any free section")
  {
    manager.register_blob({10});
    manager.register_blob({20});
    REQUIRE(manager.unregister_blob({0}, {10}) == true); // Unregister first blob to create a gap
    auto offset = manager.register_blob({15});
    REQUIRE(offset.data == 30); // Offset should be at the end of the first blob
  }
}

TEST_CASE("Incorrect unregistration", "[blob_manager_t]")
{
  blob_manager_t manager;

  SECTION("Unregister a blob that was not registered")
  {
    REQUIRE(manager.unregister_blob({100}, {10}) == false); // Trying to unregister a non-existent blob
  }
}

TEST_CASE("Unregister blob with end outside file size", "[blob_manager_t]")
{
  blob_manager_t manager;
  auto offset = manager.register_blob({50});
  REQUIRE(offset.data == 0); // Assuming initial file size is zero

  SECTION("Unregister blob that ends beyond the file size")
  {
    // Assuming current file size is now 50 due to previous registration
    REQUIRE_FALSE(manager.unregister_blob({40}, {20})); // This should fail as it would end at offset 60
  }
}

TEST_CASE("Unregister blob that starts in free space", "[blob_manager_t]")
{
  blob_manager_t manager;
  auto offset1 = manager.register_blob({10});
  auto offset2 = manager.register_blob({20});
  REQUIRE(offset1.data == 0);
  REQUIRE(offset2.data == 10);

  // Create a free section
  manager.unregister_blob(offset1, {10});

  SECTION("Unregister blob that starts in a free section")
  {
    REQUIRE_FALSE(manager.unregister_blob({5}, {10})); // This should fail as it starts within a free section
  }
}

TEST_CASE("Unregister blob that ends in free space", "[blob_manager_t]")
{
  blob_manager_t manager;
  auto offset1 = manager.register_blob({10});
  auto offset2 = manager.register_blob({20});
  auto offset3 = manager.register_blob({30});
  REQUIRE(offset1.data == 0);
  REQUIRE(offset2.data == 10);
  REQUIRE(offset3.data == 30);

  // Create a free section
  manager.unregister_blob(offset2, {20});

  SECTION("Unregister blob that ends in a free section")
  {
    REQUIRE_FALSE(manager.unregister_blob({5}, {15})); // This should fail as it ends within a free section
  }
}

TEST_CASE("Unregister blob that starts and ends in free space", "[blob_manager_t]")
{
  blob_manager_t manager;
  auto offset1 = manager.register_blob({10});
  auto offset2 = manager.register_blob({20});
  REQUIRE(offset1.data == 0);
  REQUIRE(offset2.data == 10);

  // Create a free section
  manager.unregister_blob(offset1, {10});

  SECTION("Unregister blob that encompasses a free section")
  {
    REQUIRE_FALSE(manager.unregister_blob({5}, {20})); // This should fail as it starts and ends within a free section
  }
}

// Additional tests can be written to cover more edge cases and scenarios.

TEST_CASE("Merging free spaces", "[blob_manager_t]")
{
  blob_manager_t manager;

  SECTION("Unregister and merge adjacent blobs")
  {
    auto offset1 = manager.register_blob({10});
    auto offset2 = manager.register_blob({20});
    auto offset3 = manager.register_blob({20});
    REQUIRE(manager.unregister_blob(offset1, {10}) == true);
    REQUIRE(manager.unregister_blob(offset2, {20}) == true);
    REQUIRE(manager.get_free_sections_count() == 2);
    REQUIRE(manager.get_free_section(0).size.data + manager.get_free_section(1).size.data == 30); // Assuming that manager can retrieve a free section by index
    manager.merge_free_sections();
    REQUIRE(manager.get_free_sections_count() == 1);
    REQUIRE(manager.get_free_section(0).size.data == 30); // Assuming that manager can retrieve a free section by index
  }
}
TEST_CASE("File size decreases when the last blob is unregistered", "[blob_manager_t]")
{
  blob_manager_t manager;
  auto initial_size = manager.get_file_size(); // Assuming this method exists

  SECTION("Register and unregister blobs at the end")
  {
    auto offset1 = manager.register_blob({10});
    auto offset2 = manager.register_blob({20});
    REQUIRE(offset1.data == initial_size.data);      // Offset should be at the end of the file
    REQUIRE(offset2.data == initial_size.data + 10); // Offset should be at the end of the file after offset1

    // File size should increase by 30 (size of both blobs)
    REQUIRE(manager.get_file_size().data == initial_size.data + 30);

    // Unregister the last blob
    REQUIRE(manager.unregister_blob(offset2, {20}));

    // File size should decrease by the size of the last blob
    REQUIRE(manager.get_file_size().data == initial_size.data + 10);

    // Unregister the second last blob, which is now the last blob
    REQUIRE(manager.unregister_blob(offset1, {10}));

    // File size should decrease by the size of the now last blob
    REQUIRE(manager.get_file_size().data == initial_size.data);
  }
}

TEST_CASE("Unregister blob should merge with adjacent free sections", "[blob_manager_t]")
{
  blob_manager_t manager;

  // Register and then unregister multiple blobs to create non-contiguous free sections.
  auto offset1 = manager.register_blob({10});
  auto offset2 = manager.register_blob({20});
  auto offset3 = manager.register_blob({30});
  auto offset4 = manager.register_blob({40});

  // Set up free sections that are not contiguous
  REQUIRE(manager.unregister_blob(offset1, {10}) == true); // Free section at the beginning
  REQUIRE(manager.unregister_blob(offset3, {30}) == true); // Free section in the middle

  // This leaves a used section from offset 10 to 30, surrounded by free sections.

  SECTION("Unregister a blob that bridges two free sections")
  {
    // Now unregister the blob that is between two free sections
    REQUIRE(manager.unregister_blob(offset2, {20}) == true);

    REQUIRE(manager.get_free_sections_count() == 3);

    REQUIRE(manager.get_free_section(0).size.data + manager.get_free_section(1).size.data + manager.get_free_section(2).size.data == 60);
    
    manager.merge_free_sections();
    REQUIRE(manager.get_free_sections_count() == 1);
    REQUIRE(manager.get_free_section(0).size.data == 60);
  }
}

TEST_CASE("Unregistering blob overlapping with free section should fail", "[blob_manager_t]")
{
  blob_manager_t manager;

  // Register blobs to set up the test.
  auto offset1 = manager.register_blob({10});
  auto offset2 = manager.register_blob({20});
  auto offset3 = manager.register_blob({30});

  // Unregister the middle blob to create a free section within the file.
  REQUIRE(manager.unregister_blob(offset2, {20}) == true); // Free section in the middle

  // Confirm the file size has not shrunk.
  auto current_file_size = manager.get_file_size();
  REQUIRE(current_file_size.data == offset1.data + 10 + 20 + 30);

  // Try to unregister a blob that overlaps with the free section.
  SECTION("Unregister blob that overlaps with the beginning of a free section")
  {
    REQUIRE_FALSE(manager.unregister_blob({15}, {10}));
  }

  SECTION("Unregister blob that overlaps with the end of a free section")
  {
    REQUIRE_FALSE(manager.unregister_blob({5}, {20}));
  }

  SECTION("Unregister blob that is completely within a free section")
  {
    REQUIRE_FALSE(manager.unregister_blob({12}, {5}));
  }

  SECTION("Unregister blob that spans across the entire free section")
  {
    REQUIRE_FALSE(manager.unregister_blob({5}, {30}));
  }

  REQUIRE(manager.get_file_size().data == current_file_size.data);
}
