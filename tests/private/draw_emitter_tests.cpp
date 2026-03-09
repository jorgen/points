#include <doctest/doctest.h>
#include <draw_emitter.hpp>
#include <gpu_buffer_manager.hpp>
#include <gpu_node_buffer.hpp>
#include <frame_node_registry.hpp>
#include <node_selector.hpp>
#include <renderer_callbacks.hpp>
#include <node_data_loader.hpp>

#include <unordered_map>
#include <unordered_set>

using namespace points::converter;
using namespace points::render;

// ---------------------------------------------------------------------------
// Mock callback infrastructure
// ---------------------------------------------------------------------------

struct test_callback_context_t
{
  uintptr_t counter = 0;
  std::vector<void *> destroyed_buffers;
  int modify_count = 0;
};

static test_callback_context_t *g_test_ctx = nullptr;

static void test_create_buffer(points_renderer_t *, void *, points_buffer_type_t, void **buffer_user_ptr)
{
  g_test_ctx->counter++;
  *buffer_user_ptr = reinterpret_cast<void *>(g_test_ctx->counter);
}

static void test_initialize_buffer(points_renderer_t *, void *, points_buffer_t *, void *, points_type_t, points_components_t, int, void *)
{
}

static void test_modify_buffer(points_renderer_t *, void *, points_buffer_t *, void *, int, int, void *)
{
  g_test_ctx->modify_count++;
}

static void test_destroy_buffer(points_renderer_t *, void *, void *buffer_user_ptr)
{
  g_test_ctx->destroyed_buffers.push_back(buffer_user_ptr);
}

static std::unique_ptr<callback_manager_t> make_test_callbacks(test_callback_context_t &ctx)
{
  g_test_ctx = &ctx;
  auto cbm = std::make_unique<callback_manager_t>(nullptr);
  points_renderer_callbacks_t cbs = {};
  cbs.create_buffer = test_create_buffer;
  cbs.initialize_buffer = test_initialize_buffer;
  cbs.modify_buffer = test_modify_buffer;
  cbs.destroy_buffer = test_destroy_buffer;
  cbm->set_callbacks(cbs, nullptr);
  return cbm;
}

// ---------------------------------------------------------------------------
// Mock node_data_loader_t
// ---------------------------------------------------------------------------

class mock_node_data_loader_t : public node_data_loader_t
{
public:
  std::vector<load_handle_t> cancelled_handles;
  std::unordered_set<load_handle_t> ready_handles;
  std::unordered_map<load_handle_t, loaded_node_data_t> loaded_data;
  load_handle_t next_handle = 1;

  load_handle_t request_load(const void *, uint32_t) override { return next_handle++; }
  bool is_ready(load_handle_t handle) override { return ready_handles.count(handle) > 0; }
  loaded_node_data_t get_data(load_handle_t handle) override
  {
    auto it = loaded_data.find(handle);
    if (it != loaded_data.end())
      return it->second;
    return {};
  }
  void cancel(load_handle_t handle) override { cancelled_handles.push_back(handle); }
};

// ---------------------------------------------------------------------------
// Helpers (following node_selector_tests.cpp patterns)
// ---------------------------------------------------------------------------

static node_id_t make_nid(uint32_t tree, uint16_t level, uint16_t index)
{
  node_id_t id;
  id.tree_id.data = tree;
  id.level = level;
  id.index = index;
  return id;
}

static tree_walker_data_t make_sub(node_id_t node, node_id_t parent, int lod, uint32_t point_count = 1000)
{
  tree_walker_data_t d;
  d.node = node;
  d.parent = parent;
  d.lod = lod;
  d.point_count = point_count_t(point_count);
  d.frustum_visible = true;
  d.aabb = {{0, 0, 0}, {1, 1, 1}};
  d.tight_aabb = {{0.1, 0.1, 0.1}, {0.9, 0.9, 0.9}};
  memset(d.format, 0, sizeof(d.format));
  memset(d.locations, 0, sizeof(d.locations));
  d.offset_in_subset = {};
  d.input_id = {};
  return d;
}

static frame_camera_cpp_t make_identity_camera()
{
  frame_camera_cpp_t cam;
  cam.view = glm::dmat4(1.0);
  cam.projection = glm::dmat4(1.0);
  cam.view_projection = glm::dmat4(1.0);
  cam.inverse_view = glm::dmat4(1.0);
  cam.inverse_projection = glm::dmat4(1.0);
  cam.inverse_view_projection = glm::dmat4(1.0);
  return cam;
}

static points_to_render_t *to_render_ptr(std::vector<points_draw_group_t> &vec)
{
  return reinterpret_cast<points_to_render_t *>(&vec);
}

static void make_rendered_steady(gpu_node_buffer_t &buf, test_callback_context_t &ctx)
{
  buf.rendered = true;
  buf.fade_frame = gpu_node_buffer_t::FADE_FRAMES;
  buf.draw_type = points_dyn_points_3;
  buf.point_count = 100;

  buf.render_buffers[0].user_ptr = reinterpret_cast<void *>(++ctx.counter); // vertex
  buf.render_buffers[1].user_ptr = reinterpret_cast<void *>(++ctx.counter); // color
  buf.render_buffers[2].user_ptr = reinterpret_cast<void *>(++ctx.counter); // camera

  buf.render_list[0] = {points_dyn_points_bm_vertex, buf.render_buffers[0].user_ptr};
  buf.render_list[1] = {points_dyn_points_bm_color, buf.render_buffers[1].user_ptr};
  buf.render_list[2] = {points_dyn_points_bm_camera, buf.render_buffers[2].user_ptr};

  buf.old_color_valid = false;
  buf.awaiting_new_color = false;
  buf.crossfade_frame = 0;
  buf.params_buffer = {};
  buf.gpu_memory_size = 1024;
  buf.attribute_data_size = 256;
  buf.old_color_memory = 0;
}

static void setup_single_node_registry(frame_node_registry_t &registry,
                                       const std::vector<tree_walker_data_t> &subsets,
                                       const std::vector<std::pair<node_id_t, node_id_t>> &edges,
                                       std::vector<std::unique_ptr<gpu_node_buffer_t>> &bufs)
{
  registry.update_from_walker(subsets, edges, bufs);
}

// ---------------------------------------------------------------------------
// handle_attribute_change tests
// ---------------------------------------------------------------------------

TEST_CASE("handle_attribute_change")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  auto *mock_loader = static_cast<mock_node_data_loader_t *>(loader.get());
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;
  size_t gpu_mem = 0;

  SUBCASE("saves current color to old_color_buffer")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    make_rendered_steady(*b, ctx);
    void *original_color = b->render_buffers[1].user_ptr;
    gpu_mem = b->gpu_memory_size;
    bufs.push_back(std::move(b));

    mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);

    REQUIRE(bufs[0]->old_color_valid == true);
    REQUIRE(bufs[0]->old_color_buffer.user_ptr == original_color);
    REQUIRE(bufs[0]->render_buffers[1].user_ptr == nullptr);
    REQUIRE(bufs[0]->awaiting_new_color == true);
    REQUIRE(bufs[0]->crossfade_frame == 0);
  }

  SUBCASE("already-awaiting buffer keeps existing old_color")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    make_rendered_steady(*b, ctx);
    // Simulate a first attribute change
    void *original_color = b->render_buffers[1].user_ptr;
    b->old_color_buffer.user_ptr = original_color;
    b->old_color_valid = true;
    b->render_buffers[1] = {};
    b->awaiting_new_color = true;
    gpu_mem = b->gpu_memory_size;
    bufs.push_back(std::move(b));

    mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);

    REQUIRE(bufs[0]->old_color_buffer.user_ptr == original_color);
    REQUIRE(bufs[0]->awaiting_new_color == true);
    REQUIRE(bufs[0]->old_color_valid == true);
  }

  SUBCASE("crossfading buffer gets old_color destroyed then replaced")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    make_rendered_steady(*b, ctx);
    // Set up crossfade state: old_color_valid=true, awaiting=false
    void *prev_old_color = reinterpret_cast<void *>(++ctx.counter);
    b->old_color_buffer.user_ptr = prev_old_color;
    b->old_color_valid = true;
    b->old_color_memory = 128;
    b->gpu_memory_size += 128; // old color is on GPU
    b->awaiting_new_color = false;
    void *current_color = b->render_buffers[1].user_ptr;
    gpu_mem = b->gpu_memory_size;
    bufs.push_back(std::move(b));

    size_t mem_before = gpu_mem;
    mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);

    // Previous old_color was destroyed
    REQUIRE(std::find(ctx.destroyed_buffers.begin(), ctx.destroyed_buffers.end(), prev_old_color) != ctx.destroyed_buffers.end());
    // New old_color is the current color buffer
    REQUIRE(bufs[0]->old_color_buffer.user_ptr == current_color);
    REQUIRE(bufs[0]->old_color_valid == true);
    REQUIRE(bufs[0]->awaiting_new_color == true);
    REQUIRE(bufs[0]->render_buffers[1].user_ptr == nullptr);
    // Memory accounting: old color (128) subtracted from gpu_memory_used
    REQUIRE(gpu_mem == mem_before - 128);
    // new old_color_memory tracks the current attribute_data_size
    REQUIRE(bufs[0]->old_color_memory == 256);
  }

  SUBCASE("cancels pending load handles")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    make_rendered_steady(*b, ctx);
    b->load_handle = 42;
    gpu_mem = b->gpu_memory_size;
    bufs.push_back(std::move(b));

    mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);

    REQUIRE(mock_loader->cancelled_handles.size() == 1);
    REQUIRE(mock_loader->cancelled_handles[0] == 42);
    REQUIRE(bufs[0]->load_handle == invalid_load_handle);
  }

  SUBCASE("non-rendered buffer gets destroyed entirely")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->rendered = false;
    b->load_handle = 99;
    bufs.push_back(std::move(b));

    mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);

    REQUIRE(mock_loader->cancelled_handles.size() == 1);
    REQUIRE(mock_loader->cancelled_handles[0] == 99);
    REQUIRE(bufs[0]->load_handle == invalid_load_handle);
    REQUIRE(bufs[0]->awaiting_new_color == false);
    REQUIRE(bufs[0]->rendered == false);
  }
}

// ---------------------------------------------------------------------------
// draw_emitter_t::emit() tests
// ---------------------------------------------------------------------------

TEST_CASE("draw_emitter steady state emits 3-buffer draw group")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  void *vertex_ptr = b->render_buffers[0].user_ptr;
  void *color_ptr = b->render_buffers[1].user_ptr;
  void *camera_ptr = b->render_buffers[2].user_ptr;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.size() == 1);
  REQUIRE(to_render[0].draw_type == points_dyn_points_3);
  REQUIRE(to_render[0].buffers_size == 3);
  REQUIRE(to_render[0].draw_size == 100);
  REQUIRE(to_render[0].buffers[0].buffer_mapping == points_dyn_points_bm_vertex);
  REQUIRE(to_render[0].buffers[0].user_ptr == vertex_ptr);
  REQUIRE(to_render[0].buffers[1].buffer_mapping == points_dyn_points_bm_color);
  REQUIRE(to_render[0].buffers[1].user_ptr == color_ptr);
  REQUIRE(to_render[0].buffers[2].buffer_mapping == points_dyn_points_bm_camera);
  REQUIRE(to_render[0].buffers[2].user_ptr == camera_ptr);
  REQUIRE(result.any_animating == false);
}

TEST_CASE("draw_emitter fade-in emits crossfade with incrementing alpha")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->fade_frame = 0; // start fade
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  // First emit: fade_frame goes 0 -> 1, alpha = 0.1
  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.size() == 1);
  REQUIRE(to_render[0].draw_type == points_dyn_points_crossfade);
  REQUIRE(to_render[0].buffers_size == 5);
  REQUIRE(result.any_animating == true);
  REQUIRE(bufs[0]->fade_frame == 1);

  // Emit 9 more times to complete fade
  for (int i = 0; i < 9; i++)
  {
    to_render.clear();
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }
  REQUIRE(bufs[0]->fade_frame == 10);

  // Next emit should be steady state
  to_render.clear();
  result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  REQUIRE(to_render.size() == 1);
  REQUIRE(to_render[0].draw_type == points_dyn_points_3);
  REQUIRE(to_render[0].buffers_size == 3);
  REQUIRE(result.any_animating == false);
}

TEST_CASE("draw_emitter awaiting state uses old_color_buffer for both color slots")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);

  // Set up awaiting state
  void *old_color_ptr = b->render_buffers[1].user_ptr;
  b->old_color_buffer.user_ptr = old_color_ptr;
  b->old_color_valid = true;
  b->old_color_is_mono = false;
  b->render_buffers[1] = {};
  b->awaiting_new_color = true;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.size() == 1);
  REQUIRE(to_render[0].draw_type == points_dyn_points_crossfade);
  REQUIRE(to_render[0].buffers_size == 5);
  REQUIRE(result.any_animating == true);

  // Both color and old_color should point to old_color_buffer
  REQUIRE(to_render[0].buffers[1].buffer_mapping == points_dyn_points_bm_color);
  REQUIRE(to_render[0].buffers[1].user_ptr == old_color_ptr);
  REQUIRE(to_render[0].buffers[3].buffer_mapping == points_dyn_points_bm_old_color);
  REQUIRE(to_render[0].buffers[3].user_ptr == old_color_ptr);

  // blend should be 1.0 (params_data.y)
  REQUIRE(bufs[0]->params_data.y == doctest::Approx(1.0f));
}

TEST_CASE("draw_emitter crossfade progresses when parent is not transitioning")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {
    make_sub(root, empty, 10),
    make_sub(child, root, 9),
  };
  std::vector<std::pair<node_id_t, node_id_t>> edges = {{root, child}};

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  // Parent: steady state
  auto parent_buf = std::make_unique<gpu_node_buffer_t>();
  parent_buf->node_info = subsets[0];
  make_rendered_steady(*parent_buf, ctx);
  bufs.push_back(std::move(parent_buf));

  // Child: crossfading
  auto child_buf = std::make_unique<gpu_node_buffer_t>();
  child_buf->node_info = subsets[1];
  make_rendered_steady(*child_buf, ctx);
  child_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  child_buf->old_color_valid = true;
  child_buf->crossfade_frame = 0;
  bufs.push_back(std::move(child_buf));

  setup_single_node_registry(registry, subsets, edges, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);
  selection.active_set.insert(child);

  // Emit multiple frames - crossfade should progress
  for (int i = 0; i < 10; i++)
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }

  // After 10 frames, crossfade should be complete
  REQUIRE(bufs[1]->crossfade_frame == 10);
  REQUIRE(bufs[1]->old_color_valid == false);
}

TEST_CASE("draw_emitter crossfade blocked when parent is transitioning")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {
    make_sub(root, empty, 10),
    make_sub(child, root, 9),
  };
  std::vector<std::pair<node_id_t, node_id_t>> edges = {{root, child}};

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  // Parent: also crossfading (transitioning)
  auto parent_buf = std::make_unique<gpu_node_buffer_t>();
  parent_buf->node_info = subsets[0];
  make_rendered_steady(*parent_buf, ctx);
  parent_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  parent_buf->old_color_valid = true;
  parent_buf->crossfade_frame = 0;
  bufs.push_back(std::move(parent_buf));

  // Child: also crossfading
  auto child_buf = std::make_unique<gpu_node_buffer_t>();
  child_buf->node_info = subsets[1];
  make_rendered_steady(*child_buf, ctx);
  child_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  child_buf->old_color_valid = true;
  child_buf->crossfade_frame = 0;
  bufs.push_back(std::move(child_buf));

  setup_single_node_registry(registry, subsets, edges, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);
  selection.active_set.insert(child);

  // Emit once: child's crossfade should be blocked (blend=0.0), parent should advance
  std::vector<points_draw_group_t> to_render;
  emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(bufs[1]->crossfade_frame == 0);
  REQUIRE(bufs[1]->params_data.y == doctest::Approx(0.0f));
  // Parent should be advancing
  REQUIRE(bufs[0]->crossfade_frame == 1);

  // Advance parent to completion (9 more frames)
  for (int i = 0; i < 9; i++)
  {
    to_render.clear();
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }
  REQUIRE(bufs[0]->old_color_valid == false);

  // Now child should start progressing
  to_render.clear();
  emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  REQUIRE(bufs[1]->crossfade_frame == 1);
}

TEST_CASE("draw_emitter skips non-visible and non-rendered nodes")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  SUBCASE("non-visible node produces no draw groups")
  {
    auto sub = make_sub(root, empty, 10);
    sub.frustum_visible = false;

    std::vector<tree_walker_data_t> subsets = {sub};
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = subsets[0];
    make_rendered_steady(*b, ctx);
    bufs.push_back(std::move(b));

    setup_single_node_registry(registry, subsets, {}, bufs);

    selection_result_t selection;
    selection.active_set.insert(root);

    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    REQUIRE(to_render.empty());
  }

  SUBCASE("non-rendered node produces no draw groups")
  {
    std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = subsets[0];
    b->rendered = false;
    bufs.push_back(std::move(b));

    setup_single_node_registry(registry, subsets, {}, bufs);

    selection_result_t selection;
    selection.active_set.insert(root);

    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    REQUIRE(to_render.empty());
  }
}

// ---------------------------------------------------------------------------
// Lifecycle / regression tests
// ---------------------------------------------------------------------------

TEST_CASE("full lifecycle: steady -> attribute change -> awaiting -> crossfade -> steady")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  void *original_color = b->render_buffers[1].user_ptr;
  size_t gpu_mem = b->gpu_memory_size;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  // Phase 1: steady state
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == points_dyn_points_3);
    REQUIRE(result.any_animating == false);
  }

  // Phase 2: attribute change -> awaiting
  size_t mem_before_change = gpu_mem;
  mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);
  REQUIRE(bufs[0]->awaiting_new_color == true);
  REQUIRE(bufs[0]->old_color_buffer.user_ptr == original_color);
  REQUIRE(bufs[0]->old_color_memory == 256); // attribute_data_size saved
  // gpu_memory_used unchanged — old color still on GPU, just moved to old_color_buffer
  REQUIRE(gpu_mem == mem_before_change);

  // Phase 3: emit while awaiting (old color shown for both slots)
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == points_dyn_points_crossfade);
    REQUIRE(result.any_animating == true);
    REQUIRE(to_render[0].buffers[1].user_ptr == original_color);
    REQUIRE(to_render[0].buffers[3].user_ptr == original_color);
    REQUIRE(result.freed_gpu_memory == 0);
  }

  // Phase 4: simulate new color data arrival
  // (In production this happens via upload_ready, but we manually set the state)
  void *new_color = reinterpret_cast<void *>(++ctx.counter);
  bufs[0]->render_buffers[1].user_ptr = new_color;
  size_t new_attr_size = 512;
  bufs[0]->old_color_memory = bufs[0]->attribute_data_size;
  bufs[0]->attribute_data_size = new_attr_size;
  bufs[0]->gpu_memory_size += new_attr_size;
  gpu_mem += new_attr_size;
  bufs[0]->awaiting_new_color = false;
  bufs[0]->crossfade_frame = 0;
  // old_color_valid stays true to trigger crossfade

  size_t mem_during_crossfade = gpu_mem;

  // Phase 5: emit through crossfade (10 frames)
  size_t total_freed = 0;
  for (int i = 0; i < 10; i++)
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == points_dyn_points_crossfade);
    REQUIRE(result.any_animating == true);
    total_freed += result.freed_gpu_memory;
    gpu_mem -= result.freed_gpu_memory;
  }

  // Crossfade completed on frame 10: old color memory freed
  REQUIRE(total_freed == 256); // old_color_memory was 256
  REQUIRE(bufs[0]->old_color_memory == 0);
  REQUIRE(gpu_mem == mem_during_crossfade - 256);

  // Phase 6: next emit should be back to steady state
  REQUIRE(bufs[0]->old_color_valid == false);
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == points_dyn_points_3);
    REQUIRE(to_render[0].buffers_size == 3);
    REQUIRE(result.any_animating == false);
    REQUIRE(to_render[0].buffers[1].user_ptr == new_color);
    REQUIRE(result.freed_gpu_memory == 0);
  }
}

TEST_CASE("rapid double attribute change preserves old_color")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  void *color_a = b->render_buffers[1].user_ptr;
  size_t gpu_mem = b->gpu_memory_size;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  // First attribute change
  mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);
  REQUIRE(bufs[0]->old_color_buffer.user_ptr == color_a);

  // Second attribute change before new data arrives
  mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);

  // old_color_buffer should still hold color_a (the last visible color)
  REQUIRE(bufs[0]->old_color_buffer.user_ptr == color_a);
  REQUIRE(bufs[0]->awaiting_new_color == true);

  // Emitting should still show old color correctly
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].buffers[1].user_ptr == color_a);
    REQUIRE(to_render[0].buffers[3].user_ptr == color_a);
    REQUIRE(result.any_animating == true);
  }
}

TEST_CASE("color crossfade does not block LOD fade_complete")
{
  // Regression: old_color_valid and awaiting_new_color used to set
  // all_fade_complete=false, blocking parent->child LOD swap in the
  // selector and causing 10x refine slowdown.
  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};

  SUBCASE("awaiting_new_color does not prevent all_fade_complete")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = subsets[0];
    b->rendered = true;
    b->fade_frame = gpu_node_buffer_t::FADE_FRAMES;
    b->awaiting_new_color = true;
    b->old_color_valid = true;
    bufs.push_back(std::move(b));

    frame_node_registry_t registry;
    registry.update_from_walker(subsets, {}, bufs);
    auto *node = registry.get_node(root);
    REQUIRE(node != nullptr);
    REQUIRE(node->all_rendered == true);
    REQUIRE(node->all_fade_complete == true);
  }

  SUBCASE("old_color_valid does not prevent all_fade_complete")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = subsets[0];
    b->rendered = true;
    b->fade_frame = gpu_node_buffer_t::FADE_FRAMES;
    b->old_color_valid = true;
    b->awaiting_new_color = false;
    bufs.push_back(std::move(b));

    frame_node_registry_t registry;
    registry.update_from_walker(subsets, {}, bufs);
    auto *node = registry.get_node(root);
    REQUIRE(node != nullptr);
    REQUIRE(node->all_fade_complete == true);
  }

  SUBCASE("incomplete LOD fade still blocks all_fade_complete")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = subsets[0];
    b->rendered = true;
    b->fade_frame = 5; // not yet complete
    bufs.push_back(std::move(b));

    frame_node_registry_t registry;
    registry.update_from_walker(subsets, {}, bufs);
    auto *node = registry.get_node(root);
    REQUIRE(node != nullptr);
    REQUIRE(node->all_fade_complete == false);
  }
}

TEST_CASE("child crossfade not blocked by non-active parent")
{
  // Regression: after separating color crossfade from LOD fade_complete,
  // the selector clean-swaps the parent out of the active set. If emit()
  // builds transitioning_nodes from ALL registry nodes, the parent's
  // old_color_valid blocks children's crossfade forever (parent never
  // advances because it's not emitted).
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {
    make_sub(root, empty, 10),
    make_sub(child, root, 9),
  };
  std::vector<std::pair<node_id_t, node_id_t>> edges = {{root, child}};

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  // Parent: crossfading but NOT in active set (selector did clean swap)
  auto parent_buf = std::make_unique<gpu_node_buffer_t>();
  parent_buf->node_info = subsets[0];
  make_rendered_steady(*parent_buf, ctx);
  parent_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  parent_buf->old_color_valid = true;
  parent_buf->crossfade_frame = 0;
  bufs.push_back(std::move(parent_buf));

  // Child: crossfading, IS in active set
  auto child_buf = std::make_unique<gpu_node_buffer_t>();
  child_buf->node_info = subsets[1];
  make_rendered_steady(*child_buf, ctx);
  child_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  child_buf->old_color_valid = true;
  child_buf->crossfade_frame = 0;
  bufs.push_back(std::move(child_buf));

  setup_single_node_registry(registry, subsets, edges, bufs);

  // Only child in active set — parent was swapped out by selector
  selection_result_t selection;
  selection.active_set.insert(child);

  // Child's crossfade must progress even though parent is still transitioning
  std::vector<points_draw_group_t> to_render;
  emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(bufs[1]->crossfade_frame == 1);

  // Run to completion
  for (int i = 0; i < 9; i++)
  {
    to_render.clear();
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }
  REQUIRE(bufs[1]->crossfade_frame == 10);
  REQUIRE(bufs[1]->old_color_valid == false);
}

// ---------------------------------------------------------------------------
// upload_ready deadlock regression test
// ---------------------------------------------------------------------------

TEST_CASE("upload_ready processes root node whose parent equals empty_node_id")
{
  // Regression: when a root node has node_id == {0,0,0} (empty_node_id),
  // the old top-down check would find its parent (also {0,0,0}) in the
  // awaiting set and skip it forever, blocking all children too.
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  auto *mock_loader = loader.get();
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;
  size_t gpu_mem = 0;

  // Root with node_id == {0,0,0} — same as empty_node_id
  auto root = make_nid(0, 0, 0);
  node_id_t empty = {};
  auto child = make_nid(0, 1, 0);

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  // Root buffer: rendered, awaiting new color, with a pending load
  auto root_buf = std::make_unique<gpu_node_buffer_t>();
  root_buf->node_info = make_sub(root, empty, 10);
  make_rendered_steady(*root_buf, ctx);
  root_buf->old_color_buffer.user_ptr = root_buf->render_buffers[1].user_ptr;
  root_buf->old_color_valid = true;
  root_buf->old_color_is_mono = false;
  root_buf->render_buffers[1] = {};
  root_buf->awaiting_new_color = true;

  // Assign a load handle and make it ready
  load_handle_t root_handle = 100;
  root_buf->load_handle = root_handle;
  mock_loader->ready_handles.insert(root_handle);

  // Set up loaded data with minimal attribute data
  static uint16_t dummy_attr[3] = {255, 128, 64};
  loaded_node_data_t root_data;
  root_data.attribute_data = dummy_attr;
  root_data.attribute_data_size = sizeof(dummy_attr);
  root_data.attribute_type = points_type_u16;
  root_data.attribute_components = points_components_3;
  root_data.point_count = 1;
  root_data.draw_type = points_dyn_points_3;
  mock_loader->loaded_data[root_handle] = root_data;

  gpu_mem = root_buf->gpu_memory_size;
  bufs.push_back(std::move(root_buf));

  // Child buffer: also awaiting, with a ready load
  auto child_buf = std::make_unique<gpu_node_buffer_t>();
  child_buf->node_info = make_sub(child, root, 9);
  make_rendered_steady(*child_buf, ctx);
  child_buf->old_color_buffer.user_ptr = child_buf->render_buffers[1].user_ptr;
  child_buf->old_color_valid = true;
  child_buf->old_color_is_mono = false;
  child_buf->render_buffers[1] = {};
  child_buf->awaiting_new_color = true;

  load_handle_t child_handle = 101;
  child_buf->load_handle = child_handle;
  mock_loader->ready_handles.insert(child_handle);

  loaded_node_data_t child_data;
  child_data.attribute_data = dummy_attr;
  child_data.attribute_data_size = sizeof(dummy_attr);
  child_data.attribute_type = points_type_u16;
  child_data.attribute_components = points_components_3;
  child_data.point_count = 1;
  child_data.draw_type = points_dyn_points_3;
  mock_loader->loaded_data[child_handle] = child_data;

  gpu_mem += child_buf->gpu_memory_size;
  bufs.push_back(std::move(child_buf));

  // Call upload_ready — both nodes should be processed
  int uploads = mgr.upload_ready(bufs, *callbacks, loader_ptr, gpu_mem,
                                  gpu_mem + 1024 * 1024, 10, make_identity_camera(),
                                  "intensity", 0.0, 1.0);

  REQUIRE(uploads == 2);
  REQUIRE(bufs[0]->awaiting_new_color == false);
  REQUIRE(bufs[0]->load_handle == invalid_load_handle);
  REQUIRE(bufs[1]->awaiting_new_color == false);
  REQUIRE(bufs[1]->load_handle == invalid_load_handle);
}

TEST_CASE("upload_ready awaiting uploads bypass memory budget")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  auto *mock_loader = loader.get();
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  // Buffer 0: awaiting new color, with ready load
  auto awaiting_buf = std::make_unique<gpu_node_buffer_t>();
  awaiting_buf->node_info = make_sub(root, empty, 10);
  make_rendered_steady(*awaiting_buf, ctx);
  awaiting_buf->old_color_buffer.user_ptr = awaiting_buf->render_buffers[1].user_ptr;
  awaiting_buf->old_color_valid = true;
  awaiting_buf->old_color_is_mono = false;
  awaiting_buf->render_buffers[1] = {};
  awaiting_buf->awaiting_new_color = true;

  load_handle_t awaiting_handle = 200;
  awaiting_buf->load_handle = awaiting_handle;
  mock_loader->ready_handles.insert(awaiting_handle);

  static uint16_t dummy_attr[3] = {255, 128, 64};
  loaded_node_data_t awaiting_data;
  awaiting_data.attribute_data = dummy_attr;
  awaiting_data.attribute_data_size = sizeof(dummy_attr);
  awaiting_data.attribute_type = points_type_u16;
  awaiting_data.attribute_components = points_components_3;
  awaiting_data.point_count = 1;
  awaiting_data.draw_type = points_dyn_points_3;
  mock_loader->loaded_data[awaiting_handle] = awaiting_data;

  bufs.push_back(std::move(awaiting_buf));

  // Buffer 1: normal (not awaiting), with ready load
  auto normal_buf = std::make_unique<gpu_node_buffer_t>();
  normal_buf->node_info = make_sub(child, root, 9);
  // Not rendered yet — this is a fresh node needing full upload
  normal_buf->rendered = false;

  load_handle_t normal_handle = 201;
  normal_buf->load_handle = normal_handle;
  mock_loader->ready_handles.insert(normal_handle);

  static float dummy_verts[3] = {0.0f, 0.0f, 0.0f};
  loaded_node_data_t normal_data;
  normal_data.vertex_data = dummy_verts;
  normal_data.vertex_data_size = sizeof(dummy_verts);
  normal_data.vertex_type = points_type_r32;
  normal_data.vertex_components = points_components_3;
  normal_data.attribute_data = dummy_attr;
  normal_data.attribute_data_size = sizeof(dummy_attr);
  normal_data.attribute_type = points_type_u16;
  normal_data.attribute_components = points_components_3;
  normal_data.point_count = 1;
  normal_data.draw_type = points_dyn_points_3;
  mock_loader->loaded_data[normal_handle] = normal_data;

  bufs.push_back(std::move(normal_buf));

  // Set gpu_memory_used >= upload_limit so the budget gate would normally block
  size_t gpu_mem = 1000;
  size_t upload_limit = 1000; // exactly at limit

  int uploads = mgr.upload_ready(bufs, *callbacks, loader_ptr, gpu_mem,
                                  upload_limit, 4, make_identity_camera(),
                                  "intensity", 0.0, 1.0);

  // Awaiting buffer should have been uploaded (bypasses budget)
  REQUIRE(bufs[0]->awaiting_new_color == false);
  REQUIRE(bufs[0]->load_handle == invalid_load_handle);

  // Normal buffer should NOT have been uploaded (blocked by budget)
  REQUIRE(bufs[1]->rendered == false);
  REQUIRE(bufs[1]->load_handle == normal_handle);

  REQUIRE(uploads == 1);
}

// ---------------------------------------------------------------------------
// Frustum-culled crossfade completion
// ---------------------------------------------------------------------------

TEST_CASE("frustum-culled crossfading node is force-completed")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  auto sub = make_sub(root, empty, 10);
  sub.frustum_visible = false;

  std::vector<tree_walker_data_t> subsets = {sub};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->awaiting_new_color = false;
  b->old_color_memory = 512;
  b->gpu_memory_size = 2048;
  b->crossfade_frame = 3;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.empty());
  REQUIRE(bufs[0]->old_color_valid == false);
  REQUIRE(bufs[0]->old_color_buffer.user_ptr == nullptr);
  REQUIRE(result.freed_gpu_memory == 512);
  REQUIRE(bufs[0]->gpu_memory_size == 2048 - 512);
  REQUIRE(bufs[0]->old_color_memory == 0);
}

TEST_CASE("frustum-culled awaiting node is NOT force-completed")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  auto sub = make_sub(root, empty, 10);
  sub.frustum_visible = false;

  std::vector<tree_walker_data_t> subsets = {sub};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->awaiting_new_color = true;
  b->old_color_memory = 512;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(bufs[0]->old_color_valid == true);
  REQUIRE(bufs[0]->old_color_buffer.user_ptr != nullptr);
  REQUIRE(result.freed_gpu_memory == 0);
}

TEST_CASE("node transitions from visible crossfading to frustum-culled mid-crossfade")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  // Start visible and crossfading
  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->awaiting_new_color = false;
  b->old_color_memory = 256;
  b->gpu_memory_size = 1024;
  b->crossfade_frame = 0;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  // Emit 3 frames while visible — crossfade advances
  for (int i = 0; i < 3; i++)
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }
  REQUIRE(bufs[0]->crossfade_frame == 3);

  // Now frustum-cull the node: update subset and re-register
  subsets[0].frustum_visible = false;
  bufs[0]->node_info = subsets[0];
  setup_single_node_registry(registry, subsets, {}, bufs);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.empty());
  REQUIRE(bufs[0]->old_color_valid == false);
  REQUIRE(bufs[0]->old_color_buffer.user_ptr == nullptr);
  REQUIRE(result.freed_gpu_memory == 256);
}

TEST_CASE("multiple frustum-culled nodes at different stages get force-completed")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto node_a = make_nid(1, 0, 0);
  auto node_b = make_nid(1, 0, 1);
  node_id_t empty = {};

  auto sub_a = make_sub(node_a, empty, 10);
  sub_a.frustum_visible = false;
  auto sub_b = make_sub(node_b, empty, 10);
  sub_b.frustum_visible = false;

  std::vector<tree_walker_data_t> subsets = {sub_a, sub_b};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  auto buf_a = std::make_unique<gpu_node_buffer_t>();
  buf_a->node_info = subsets[0];
  make_rendered_steady(*buf_a, ctx);
  buf_a->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  buf_a->old_color_valid = true;
  buf_a->awaiting_new_color = false;
  buf_a->old_color_memory = 100;
  buf_a->gpu_memory_size = 500;
  buf_a->crossfade_frame = 2;
  bufs.push_back(std::move(buf_a));

  auto buf_b = std::make_unique<gpu_node_buffer_t>();
  buf_b->node_info = subsets[1];
  make_rendered_steady(*buf_b, ctx);
  buf_b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  buf_b->old_color_valid = true;
  buf_b->awaiting_new_color = false;
  buf_b->old_color_memory = 200;
  buf_b->gpu_memory_size = 700;
  buf_b->crossfade_frame = 7;
  bufs.push_back(std::move(buf_b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(node_a);
  selection.active_set.insert(node_b);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.empty());
  REQUIRE(bufs[0]->old_color_valid == false);
  REQUIRE(bufs[0]->old_color_buffer.user_ptr == nullptr);
  REQUIRE(bufs[1]->old_color_valid == false);
  REQUIRE(bufs[1]->old_color_buffer.user_ptr == nullptr);
  REQUIRE(result.freed_gpu_memory == 300);
  REQUIRE(bufs[0]->gpu_memory_size == 400);
  REQUIRE(bufs[1]->gpu_memory_size == 500);
}

// ---------------------------------------------------------------------------
// Params correctness
// ---------------------------------------------------------------------------

TEST_CASE("awaiting state params: blend=1, new_is_mono copies old_color_is_mono")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};

  auto setup_awaiting = [&](bool old_is_mono) {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = subsets[0];
    make_rendered_steady(*b, ctx);
    b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
    b->old_color_valid = true;
    b->old_color_is_mono = old_is_mono;
    b->render_buffers[1] = {};
    b->awaiting_new_color = true;
    bufs.push_back(std::move(b));
    return bufs;
  };

  SUBCASE("old_color_is_mono=true")
  {
    auto bufs = setup_awaiting(true);
    setup_single_node_registry(registry, subsets, {}, bufs);

    selection_result_t selection;
    selection.active_set.insert(root);

    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    // params: {fade_alpha, blend=1.0, old_is_mono=0.0, new_is_mono=old_color_is_mono}
    REQUIRE(bufs[0]->params_data.y == doctest::Approx(1.0f));
    REQUIRE(bufs[0]->params_data.z == doctest::Approx(0.0f));
    REQUIRE(bufs[0]->params_data.w == doctest::Approx(1.0f));
  }

  SUBCASE("old_color_is_mono=false")
  {
    auto bufs = setup_awaiting(false);
    setup_single_node_registry(registry, subsets, {}, bufs);

    selection_result_t selection;
    selection.active_set.insert(root);

    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    REQUIRE(bufs[0]->params_data.y == doctest::Approx(1.0f));
    REQUIRE(bufs[0]->params_data.z == doctest::Approx(0.0f));
    REQUIRE(bufs[0]->params_data.w == doctest::Approx(0.0f));
  }
}

TEST_CASE("crossfading state params: blend ramps, mono flags from old_color_is_mono and draw_type")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->old_color_is_mono = true;
  b->awaiting_new_color = false;
  b->crossfade_frame = 0;
  b->draw_type = points_dyn_points_3; // new is RGB
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  for (int frame = 1; frame <= 10; frame++)
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    if (frame < 10)
    {
      float expected_blend = float(frame) / 10.0f;
      REQUIRE(bufs[0]->params_data.y == doctest::Approx(expected_blend));
      REQUIRE(bufs[0]->params_data.z == doctest::Approx(1.0f)); // old mono
      REQUIRE(bufs[0]->params_data.w == doctest::Approx(0.0f)); // new RGB
    }
  }

  // After 10 frames, crossfade completes: blend reached 1.0 and old_color is freed
  REQUIRE(bufs[0]->old_color_valid == false);
  REQUIRE(bufs[0]->crossfade_frame == 10);
}

TEST_CASE("fade-only state params: fade_alpha ramps, blend=1.0")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->fade_frame = 0; // trigger fade-in
  b->draw_type = points_dyn_points_1; // mono
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  for (int frame = 1; frame <= 10; frame++)
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    float expected_alpha = float(frame) / 10.0f;
    REQUIRE(bufs[0]->params_data.x == doctest::Approx(expected_alpha));
    REQUIRE(bufs[0]->params_data.y == doctest::Approx(1.0f));  // blend (no crossfade)
    REQUIRE(bufs[0]->params_data.z == doctest::Approx(0.0f));  // old_is_mono (always 0 in fade-only)
    REQUIRE(bufs[0]->params_data.w == doctest::Approx(1.0f));  // new_is_mono (points_dyn_points_1)
  }
}

TEST_CASE("parent-blocked crossfade params: blend stays 0.0, crossfade_frame stays 0")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto parent_id = make_nid(1, 0, 0);
  auto child_id = make_nid(1, 1, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {
    make_sub(parent_id, empty, 10),
    make_sub(child_id, parent_id, 9),
  };
  std::vector<std::pair<node_id_t, node_id_t>> edges = {{parent_id, child_id}};

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  // Parent: crossfading
  auto parent_buf = std::make_unique<gpu_node_buffer_t>();
  parent_buf->node_info = subsets[0];
  make_rendered_steady(*parent_buf, ctx);
  parent_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  parent_buf->old_color_valid = true;
  parent_buf->crossfade_frame = 0;
  bufs.push_back(std::move(parent_buf));

  // Child: crossfading
  auto child_buf = std::make_unique<gpu_node_buffer_t>();
  child_buf->node_info = subsets[1];
  make_rendered_steady(*child_buf, ctx);
  child_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  child_buf->old_color_valid = true;
  child_buf->crossfade_frame = 0;
  bufs.push_back(std::move(child_buf));

  setup_single_node_registry(registry, subsets, edges, bufs);

  selection_result_t selection;
  selection.active_set.insert(parent_id);
  selection.active_set.insert(child_id);

  std::vector<points_draw_group_t> to_render;
  emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  // Child blocked: blend=0.0, crossfade_frame stays 0
  REQUIRE(bufs[1]->params_data.y == doctest::Approx(0.0f));
  REQUIRE(bufs[1]->crossfade_frame == 0);

  // Parent advancing: blend=0.1, crossfade_frame=1
  REQUIRE(bufs[0]->params_data.y == doctest::Approx(0.1f));
  REQUIRE(bufs[0]->crossfade_frame == 1);
}

// ---------------------------------------------------------------------------
// Memory accounting
// ---------------------------------------------------------------------------

TEST_CASE("natural crossfade completion frees memory on completion frame only")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->awaiting_new_color = false;
  b->old_color_memory = 768;
  b->gpu_memory_size = 2048;
  b->crossfade_frame = 0;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  // Frames 1-9: no memory freed
  for (int i = 0; i < 9; i++)
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(result.freed_gpu_memory == 0);
  }

  // Frame 10: crossfade completes, memory freed
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(result.freed_gpu_memory == 768);
  }

  REQUIRE(bufs[0]->gpu_memory_size == 2048 - 768);
}

TEST_CASE("frustum-cull force-completion frees memory correctly")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  auto sub = make_sub(root, empty, 10);
  sub.frustum_visible = false;

  std::vector<tree_walker_data_t> subsets = {sub};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->awaiting_new_color = false;
  b->old_color_memory = 1024;
  b->gpu_memory_size = 3072;
  b->crossfade_frame = 5;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(result.freed_gpu_memory == 1024);
  REQUIRE(bufs[0]->gpu_memory_size == 2048);
}

TEST_CASE("cumulative freed_gpu_memory across visible completion and frustum-cull in same emit")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto node_a = make_nid(1, 0, 0);
  auto node_b = make_nid(1, 0, 1);
  node_id_t empty = {};

  // Node A: visible, crossfade_frame=9 (completes this frame)
  auto sub_a = make_sub(node_a, empty, 10);
  sub_a.frustum_visible = true;

  // Node B: frustum-culled, crossfading
  auto sub_b = make_sub(node_b, empty, 10);
  sub_b.frustum_visible = false;

  std::vector<tree_walker_data_t> subsets = {sub_a, sub_b};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  auto buf_a = std::make_unique<gpu_node_buffer_t>();
  buf_a->node_info = subsets[0];
  make_rendered_steady(*buf_a, ctx);
  buf_a->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  buf_a->old_color_valid = true;
  buf_a->awaiting_new_color = false;
  buf_a->old_color_memory = 100;
  buf_a->crossfade_frame = 9; // will reach 10 this frame
  bufs.push_back(std::move(buf_a));

  auto buf_b = std::make_unique<gpu_node_buffer_t>();
  buf_b->node_info = subsets[1];
  make_rendered_steady(*buf_b, ctx);
  buf_b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  buf_b->old_color_valid = true;
  buf_b->awaiting_new_color = false;
  buf_b->old_color_memory = 300;
  buf_b->crossfade_frame = 4;
  bufs.push_back(std::move(buf_b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(node_a);
  selection.active_set.insert(node_b);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(result.freed_gpu_memory == 400);
  REQUIRE(bufs[0]->old_color_valid == false);
  REQUIRE(bufs[1]->old_color_valid == false);
}

// ---------------------------------------------------------------------------
// Multi-node hierarchy
// ---------------------------------------------------------------------------

TEST_CASE("three-level hierarchy: grandparent blocks parent blocks child")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto grandparent = make_nid(1, 0, 0);
  auto parent_id = make_nid(1, 1, 0);
  auto child_id = make_nid(1, 2, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {
    make_sub(grandparent, empty, 10),
    make_sub(parent_id, grandparent, 9),
    make_sub(child_id, parent_id, 8),
  };
  std::vector<std::pair<node_id_t, node_id_t>> edges = {
    {grandparent, parent_id},
    {parent_id, child_id},
  };

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  auto gp_buf = std::make_unique<gpu_node_buffer_t>();
  gp_buf->node_info = subsets[0];
  make_rendered_steady(*gp_buf, ctx);
  gp_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  gp_buf->old_color_valid = true;
  gp_buf->crossfade_frame = 0;
  bufs.push_back(std::move(gp_buf));

  auto p_buf = std::make_unique<gpu_node_buffer_t>();
  p_buf->node_info = subsets[1];
  make_rendered_steady(*p_buf, ctx);
  p_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  p_buf->old_color_valid = true;
  p_buf->crossfade_frame = 0;
  bufs.push_back(std::move(p_buf));

  auto c_buf = std::make_unique<gpu_node_buffer_t>();
  c_buf->node_info = subsets[2];
  make_rendered_steady(*c_buf, ctx);
  c_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  c_buf->old_color_valid = true;
  c_buf->crossfade_frame = 0;
  bufs.push_back(std::move(c_buf));

  setup_single_node_registry(registry, subsets, edges, bufs);

  selection_result_t selection;
  selection.active_set.insert(grandparent);
  selection.active_set.insert(parent_id);
  selection.active_set.insert(child_id);

  // Phase 1: grandparent completes in 10 frames, parent and child blocked
  for (int i = 0; i < 10; i++)
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }

  REQUIRE(bufs[0]->old_color_valid == false); // grandparent done
  REQUIRE(bufs[1]->crossfade_frame == 0);     // parent was blocked
  REQUIRE(bufs[2]->crossfade_frame == 0);     // child was blocked

  // Phase 2: parent completes in 10 frames, child still blocked
  for (int i = 0; i < 10; i++)
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }

  REQUIRE(bufs[1]->old_color_valid == false); // parent done
  REQUIRE(bufs[2]->crossfade_frame == 0);     // child was blocked

  // Phase 3: child completes in 10 frames
  for (int i = 0; i < 10; i++)
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }

  REQUIRE(bufs[2]->old_color_valid == false); // child done
  REQUIRE(bufs[2]->crossfade_frame == 10);
}

TEST_CASE("sibling nodes crossfade independently when parent is steady")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  auto sib_a = make_nid(1, 1, 0);
  auto sib_b = make_nid(1, 1, 1);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {
    make_sub(root, empty, 10),
    make_sub(sib_a, root, 9),
    make_sub(sib_b, root, 9),
  };
  std::vector<std::pair<node_id_t, node_id_t>> edges = {
    {root, sib_a},
    {root, sib_b},
  };

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  // Root: steady
  auto root_buf = std::make_unique<gpu_node_buffer_t>();
  root_buf->node_info = subsets[0];
  make_rendered_steady(*root_buf, ctx);
  bufs.push_back(std::move(root_buf));

  // Sibling A: crossfading
  auto a_buf = std::make_unique<gpu_node_buffer_t>();
  a_buf->node_info = subsets[1];
  make_rendered_steady(*a_buf, ctx);
  a_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  a_buf->old_color_valid = true;
  a_buf->crossfade_frame = 0;
  bufs.push_back(std::move(a_buf));

  // Sibling B: crossfading
  auto b_buf = std::make_unique<gpu_node_buffer_t>();
  b_buf->node_info = subsets[2];
  make_rendered_steady(*b_buf, ctx);
  b_buf->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b_buf->old_color_valid = true;
  b_buf->crossfade_frame = 0;
  bufs.push_back(std::move(b_buf));

  setup_single_node_registry(registry, subsets, edges, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);
  selection.active_set.insert(sib_a);
  selection.active_set.insert(sib_b);

  // Both siblings should advance in parallel
  for (int i = 0; i < 10; i++)
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(bufs[1]->crossfade_frame == i + 1);
    REQUIRE(bufs[2]->crossfade_frame == i + 1);
  }

  REQUIRE(bufs[1]->old_color_valid == false);
  REQUIRE(bufs[2]->old_color_valid == false);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_CASE("concurrent fade-in and crossfade (both active simultaneously)")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->fade_frame = 0;
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->awaiting_new_color = false;
  b->crossfade_frame = 0;
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  // Both fade and crossfade should progress simultaneously
  for (int i = 0; i < 10; i++)
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(result.any_animating == true);
    REQUIRE(bufs[0]->fade_frame == i + 1);
    REQUIRE(bufs[0]->crossfade_frame == i + 1);
  }

  // Both should be complete
  REQUIRE(bufs[0]->fade_frame == 10);
  REQUIRE(bufs[0]->crossfade_frame == 10);
  REQUIRE(bufs[0]->old_color_valid == false);

  // Frame 11: steady state
  {
    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == points_dyn_points_3);
    REQUIRE(result.any_animating == false);
  }
}

TEST_CASE("mono to RGB crossfade sets correct is_mono flags")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->old_color_is_mono = true;
  b->awaiting_new_color = false;
  b->crossfade_frame = 0;
  b->draw_type = points_dyn_points_3; // new is RGB
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  std::vector<points_draw_group_t> to_render;
  emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(bufs[0]->params_data.z == doctest::Approx(1.0f)); // old mono
  REQUIRE(bufs[0]->params_data.w == doctest::Approx(0.0f)); // new RGB
}

TEST_CASE("RGB to mono crossfade sets correct is_mono flags")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->old_color_is_mono = false;
  b->awaiting_new_color = false;
  b->crossfade_frame = 0;
  b->draw_type = points_dyn_points_1; // new is mono
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  std::vector<points_draw_group_t> to_render;
  emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(bufs[0]->params_data.z == doctest::Approx(0.0f)); // old RGB
  REQUIRE(bufs[0]->params_data.w == doctest::Approx(1.0f)); // new mono
}

TEST_CASE("mixed visible and culled nodes in same emit")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto node_a = make_nid(1, 0, 0);
  auto node_b = make_nid(1, 0, 1);
  node_id_t empty = {};

  // Node A: visible + crossfading
  auto sub_a = make_sub(node_a, empty, 10);
  sub_a.frustum_visible = true;

  // Node B: culled + crossfading
  auto sub_b = make_sub(node_b, empty, 10);
  sub_b.frustum_visible = false;

  std::vector<tree_walker_data_t> subsets = {sub_a, sub_b};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;

  auto buf_a = std::make_unique<gpu_node_buffer_t>();
  buf_a->node_info = subsets[0];
  make_rendered_steady(*buf_a, ctx);
  buf_a->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  buf_a->old_color_valid = true;
  buf_a->awaiting_new_color = false;
  buf_a->crossfade_frame = 0;
  bufs.push_back(std::move(buf_a));

  auto buf_b = std::make_unique<gpu_node_buffer_t>();
  buf_b->node_info = subsets[1];
  make_rendered_steady(*buf_b, ctx);
  buf_b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  buf_b->old_color_valid = true;
  buf_b->awaiting_new_color = false;
  buf_b->old_color_memory = 256;
  buf_b->crossfade_frame = 3;
  bufs.push_back(std::move(buf_b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(node_a);
  selection.active_set.insert(node_b);

  std::vector<points_draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  // A produces a draw group and advances
  REQUIRE(to_render.size() == 1);
  REQUIRE(bufs[0]->crossfade_frame == 1);

  // B is force-completed with no draw group
  REQUIRE(bufs[1]->old_color_valid == false);
  REQUIRE(bufs[1]->old_color_buffer.user_ptr == nullptr);
  REQUIRE(result.freed_gpu_memory == 256);
}

// ---------------------------------------------------------------------------
// Steady-state cleanup
// ---------------------------------------------------------------------------

TEST_CASE("params_buffer lifecycle across crossfade to steady transition")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  auto b = std::make_unique<gpu_node_buffer_t>();
  b->node_info = subsets[0];
  make_rendered_steady(*b, ctx);
  b->old_color_buffer.user_ptr = reinterpret_cast<void *>(++ctx.counter);
  b->old_color_valid = true;
  b->awaiting_new_color = false;
  b->crossfade_frame = 0;
  REQUIRE(b->params_buffer.user_ptr == nullptr); // starts null
  bufs.push_back(std::move(b));

  setup_single_node_registry(registry, subsets, {}, bufs);

  selection_result_t selection;
  selection.active_set.insert(root);

  SUBCASE("params_buffer created lazily on first animating frame")
  {
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    REQUIRE(bufs[0]->params_buffer.user_ptr != nullptr);
    REQUIRE(to_render[0].buffers[4].buffer_mapping == points_dyn_points_bm_params);
    REQUIRE(to_render[0].buffers[4].user_ptr == bufs[0]->params_buffer.user_ptr);
  }

  SUBCASE("params_buffer destroyed when returning from crossfade to steady")
  {
    // Complete the crossfade
    for (int i = 0; i < 10; i++)
    {
      std::vector<points_draw_group_t> to_render;
      emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    }

    void *params_ptr = bufs[0]->params_buffer.user_ptr;
    // params_buffer may have been nulled already by do_destroy_buffer on the completion frame
    // if crossfade completes, the node transitions to steady on the NEXT emit
    // Actually, completion happens mid-frame (blend>=1.0), is_crossfading becomes false,
    // but the node still takes the animating path because fade may still be active.
    // Let's check: after 10 frames, fade_frame has been incremented 10 times from its initial
    // value of FADE_FRAMES (10), so fade_frame = 20. So is_fading is false.
    // And is_crossfading was set to false on frame 10. So the next emit goes to steady.

    // Emit one more frame — should be steady and destroy params_buffer
    ctx.destroyed_buffers.clear();
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    REQUIRE(to_render.size() == 1);
    REQUIRE(to_render[0].draw_type == points_dyn_points_3);
    REQUIRE(to_render[0].buffers_size == 3);
    // params_buffer should have been destroyed (user_ptr nulled by do_destroy_buffer)
    REQUIRE(bufs[0]->params_buffer.user_ptr == nullptr);
  }

  SUBCASE("params_buffer not double-destroyed on consecutive steady frames")
  {
    // Complete the crossfade
    for (int i = 0; i < 10; i++)
    {
      std::vector<points_draw_group_t> to_render;
      emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    }

    // First steady frame — destroys params_buffer
    {
      std::vector<points_draw_group_t> to_render;
      emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    }

    size_t destroy_count_after_first_steady = ctx.destroyed_buffers.size();

    // Second steady frame — should NOT call destroy again (user_ptr is null)
    {
      std::vector<points_draw_group_t> to_render;
      emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    }

    REQUIRE(ctx.destroyed_buffers.size() == destroy_count_after_first_steady);

    // Third steady frame — same
    {
      std::vector<points_draw_group_t> to_render;
      emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    }

    REQUIRE(ctx.destroyed_buffers.size() == destroy_count_after_first_steady);
  }
}

// ---------------------------------------------------------------------------
// prepare_fade_outs tests
// ---------------------------------------------------------------------------

TEST_CASE("prepare_fade_outs detects nodes leaving active set")
{
  draw_emitter_t emitter;

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> frame1_subsets = {
    make_sub(root, empty, 10),
    make_sub(child, root, 9),
  };

  // Frame 1: both nodes in walker — seed prev_active_set via emit
  {
    test_callback_context_t ctx;
    auto callbacks = make_test_callbacks(ctx);
    frame_node_registry_t registry;
    tree_config_t tree_config = {};

    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    for (auto &s : frame1_subsets)
    {
      auto b = std::make_unique<gpu_node_buffer_t>();
      b->node_info = s;
      make_rendered_steady(*b, ctx);
      bufs.push_back(std::move(b));
    }

    setup_single_node_registry(registry, frame1_subsets, {{root, child}}, bufs);

    selection_result_t selection;
    selection.active_set.insert(root);
    selection.active_set.insert(child);

    // First prepare + emit to seed prev_active_set
    auto &retain1 = emitter.prepare_fade_outs(frame1_subsets);
    REQUIRE(retain1.empty()); // nothing fading yet

    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }

  // Frame 2: only root in walker — child should start fading out
  {
    std::vector<tree_walker_data_t> frame2_subsets = {make_sub(root, empty, 10)};
    auto &retain2 = emitter.prepare_fade_outs(frame2_subsets);
    REQUIRE(retain2.size() == 1);
    REQUIRE(retain2.count(child) == 1);
  }
}

TEST_CASE("prepare_fade_outs removes reappearing nodes from fading set")
{
  draw_emitter_t emitter;

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  // Frame 1: both nodes active — seed prev_active_set
  {
    test_callback_context_t ctx;
    auto callbacks = make_test_callbacks(ctx);
    frame_node_registry_t registry;
    tree_config_t tree_config = {};

    std::vector<tree_walker_data_t> subsets = {
      make_sub(root, empty, 10),
      make_sub(child, root, 9),
    };

    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    for (auto &s : subsets)
    {
      auto b = std::make_unique<gpu_node_buffer_t>();
      b->node_info = s;
      make_rendered_steady(*b, ctx);
      bufs.push_back(std::move(b));
    }

    setup_single_node_registry(registry, subsets, {{root, child}}, bufs);

    selection_result_t selection;
    selection.active_set.insert(root);
    selection.active_set.insert(child);

    emitter.prepare_fade_outs(subsets);
    std::vector<points_draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }

  // Frame 2: child gone — starts fading
  {
    std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
    auto &retain = emitter.prepare_fade_outs(subsets);
    REQUIRE(retain.count(child) == 1);
  }

  // Frame 3: child comes back — should be removed from fading set
  {
    std::vector<tree_walker_data_t> subsets = {
      make_sub(root, empty, 10),
      make_sub(child, root, 9),
    };
    auto &retain = emitter.prepare_fade_outs(subsets);
    REQUIRE(retain.count(child) == 0);
    REQUIRE(retain.empty());
  }
}

TEST_CASE("prepare_fade_outs returns empty on first frame")
{
  draw_emitter_t emitter;

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};
  auto &retain = emitter.prepare_fade_outs(subsets);
  REQUIRE(retain.empty());
}

// ---------------------------------------------------------------------------
// reconcile with fade_out_retain tests
// ---------------------------------------------------------------------------

TEST_CASE("reconcile retains fade-out buffers instead of destroying them")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;
  size_t gpu_mem = 0;

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  // Build initial render_buffers for both nodes (sorted by node_info)
  auto root_sub = make_sub(root, empty, 10);
  auto child_sub = make_sub(child, root, 9);
  // reconcile expects sorted order: lod ascending then node ascending
  // root lod=10, child lod=9 → child first (lod 9 < lod 10)
  std::vector<tree_walker_data_t> initial_subsets = {child_sub, root_sub};
  std::sort(initial_subsets.begin(), initial_subsets.end(), [](const tree_walker_data_t &a, const tree_walker_data_t &b) {
    if (a.lod == b.lod) return (a.node <=> b.node) == std::strong_ordering::less;
    return a.lod < b.lod;
  });

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  for (auto &s : initial_subsets)
  {
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = s;
    make_rendered_steady(*b, ctx);
    gpu_mem += b->gpu_memory_size;
    bufs.push_back(std::move(b));
  }

  // Reconcile with only root in walker — child would normally be destroyed
  std::vector<tree_walker_data_t> walker_subsets = {root_sub};
  std::sort(walker_subsets.begin(), walker_subsets.end(), [](const tree_walker_data_t &a, const tree_walker_data_t &b) {
    if (a.lod == b.lod) return (a.node <=> b.node) == std::strong_ordering::less;
    return a.lod < b.lod;
  });

  // But child is in the retain set — should survive
  frame_node_registry_t::node_set_t retain;
  retain.insert(child);

  size_t mem_before = gpu_mem;
  int destroyed = 0;
  mgr.reconcile(bufs, walker_subsets, *callbacks, loader_ptr, gpu_mem, false, &destroyed, retain);

  REQUIRE(destroyed == 0);
  REQUIRE(gpu_mem == mem_before); // no memory freed

  // Both buffers should still exist
  REQUIRE(bufs.size() == 2);

  // Retained buffer should have frustum_visible set to false
  bool found_child = false;
  for (auto &b : bufs)
  {
    if ((b->node_info.node <=> child) == std::strong_ordering::equal)
    {
      REQUIRE(b->node_info.frustum_visible == false);
      REQUIRE(b->rendered == true); // buffers preserved
      found_child = true;
    }
  }
  REQUIRE(found_child);
}

TEST_CASE("reconcile destroys non-retained buffers normally")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;
  size_t gpu_mem = 0;

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  auto root_sub = make_sub(root, empty, 10);
  auto child_sub = make_sub(child, root, 9);
  std::vector<tree_walker_data_t> initial_subsets = {child_sub, root_sub};
  std::sort(initial_subsets.begin(), initial_subsets.end(), [](const tree_walker_data_t &a, const tree_walker_data_t &b) {
    if (a.lod == b.lod) return (a.node <=> b.node) == std::strong_ordering::less;
    return a.lod < b.lod;
  });

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  for (auto &s : initial_subsets)
  {
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = s;
    make_rendered_steady(*b, ctx);
    gpu_mem += b->gpu_memory_size;
    bufs.push_back(std::move(b));
  }

  // Reconcile with only root — child NOT in retain set → destroyed
  std::vector<tree_walker_data_t> walker_subsets = {root_sub};
  std::sort(walker_subsets.begin(), walker_subsets.end(), [](const tree_walker_data_t &a, const tree_walker_data_t &b) {
    if (a.lod == b.lod) return (a.node <=> b.node) == std::strong_ordering::less;
    return a.lod < b.lod;
  });

  frame_node_registry_t::node_set_t empty_retain;
  int destroyed = 0;
  mgr.reconcile(bufs, walker_subsets, *callbacks, loader_ptr, gpu_mem, false, &destroyed, empty_retain);

  REQUIRE(destroyed == 1);
  REQUIRE(bufs.size() == 1);
}

// ---------------------------------------------------------------------------
// update_from_walker with fade_out_retain tests
// ---------------------------------------------------------------------------

TEST_CASE("registry retains fade-out nodes not in walker output")
{
  frame_node_registry_t registry;

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  // Frame 1: both nodes
  std::vector<tree_walker_data_t> subsets1 = {
    make_sub(root, empty, 10),
    make_sub(child, root, 9),
  };
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs1;
  for (auto &s : subsets1)
  {
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = s;
    bufs1.push_back(std::move(b));
  }
  registry.update_from_walker(subsets1, {{root, child}}, bufs1);
  REQUIRE(registry.get_node(child) != nullptr);

  // Frame 2: only root in walker, but child in retain set
  std::vector<tree_walker_data_t> subsets2 = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs2;
  auto b2 = std::make_unique<gpu_node_buffer_t>();
  b2->node_info = subsets2[0];
  bufs2.push_back(std::move(b2));

  frame_node_registry_t::node_set_t retain;
  retain.insert(child);

  auto diff = registry.update_from_walker(subsets2, {}, bufs2, retain);

  // Child should NOT be removed
  REQUIRE(registry.get_node(child) != nullptr);
  // Child should not be in the removed list
  for (auto &r : diff.removed)
    REQUIRE_FALSE((r <=> child) == std::strong_ordering::equal);
}

TEST_CASE("registry removes nodes not in walker and not in retain set")
{
  frame_node_registry_t registry;

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets1 = {
    make_sub(root, empty, 10),
    make_sub(child, root, 9),
  };
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs1;
  for (auto &s : subsets1)
  {
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = s;
    bufs1.push_back(std::move(b));
  }
  registry.update_from_walker(subsets1, {{root, child}}, bufs1);

  // Frame 2: only root, empty retain → child should be removed
  std::vector<tree_walker_data_t> subsets2 = {make_sub(root, empty, 10)};
  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs2;
  auto b2 = std::make_unique<gpu_node_buffer_t>();
  b2->node_info = subsets2[0];
  bufs2.push_back(std::move(b2));

  auto diff = registry.update_from_walker(subsets2, {}, bufs2, {});

  REQUIRE(registry.get_node(child) == nullptr);
  REQUIRE(diff.removed.size() == 1);
}

// ---------------------------------------------------------------------------
// evict with fade_out_retain tests
// ---------------------------------------------------------------------------

TEST_CASE("evict does not evict fade-out retained nodes")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;

  auto root = make_nid(1, 0, 0);
  auto extra = make_nid(1, 0, 1); // a second root-level node
  node_id_t empty = {};

  frame_node_registry_t registry;
  std::vector<tree_walker_data_t> subsets = {
    make_sub(root, empty, 10),
    make_sub(extra, empty, 10),
  };

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  size_t gpu_mem = 0;
  for (auto &s : subsets)
  {
    auto b = std::make_unique<gpu_node_buffer_t>();
    b->node_info = s;
    make_rendered_steady(*b, ctx);
    b->gpu_memory_size = 512 * 1024; // 512KB each
    gpu_mem += b->gpu_memory_size;
    bufs.push_back(std::move(b));
  }

  registry.update_from_walker(subsets, {}, bufs);

  // Only root is active — extra is evictable normally
  selection_result_t selection;
  selection.active_set.insert(root);

  // But extra is in fade_out_retain → should not be evicted
  frame_node_registry_t::node_set_t retain;
  retain.insert(extra);

  glm::dvec3 cam_pos(0, 0, 0);
  size_t target = 256 * 1024; // force eviction pressure (target < used)
  int evicted = 0;
  mgr.evict(bufs, registry, selection, cam_pos, gpu_mem, target, *callbacks, loader_ptr, false, &evicted, retain);

  REQUIRE(evicted == 0); // extra protected by retain set
}

// ---------------------------------------------------------------------------
// Full fade-out lifecycle integration test
// ---------------------------------------------------------------------------

TEST_CASE("full fade-out lifecycle: node leaves walker, fades out, completes")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  auto child = make_nid(1, 1, 0);
  node_id_t empty = {};

  auto root_sub = make_sub(root, empty, 10);
  auto child_sub = make_sub(child, root, 9);

  auto sort_subsets = [](std::vector<tree_walker_data_t> &v) {
    std::sort(v.begin(), v.end(), [](const tree_walker_data_t &a, const tree_walker_data_t &b) {
      if (a.lod == b.lod) return (a.node <=> b.node) == std::strong_ordering::less;
      return a.lod < b.lod;
    });
  };

  auto loader = std::make_unique<mock_node_data_loader_t>();
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;
  frame_node_registry_t registry;
  size_t gpu_mem = 0;

  // === Frame 1: both nodes visible ===
  {
    std::vector<tree_walker_data_t> walker = {root_sub, child_sub};
    sort_subsets(walker);

    // Build initial buffers via reconcile
    std::vector<std::unique_ptr<gpu_node_buffer_t>> empty_bufs;
    mgr.reconcile(empty_bufs, walker, *callbacks, loader_ptr, gpu_mem);
    // Manually make them rendered
    for (auto &b : empty_bufs)
    {
      make_rendered_steady(*b, ctx);
      gpu_mem += b->gpu_memory_size;
    }

    auto &retain = emitter.prepare_fade_outs(walker);
    REQUIRE(retain.empty());

    mgr.reconcile(empty_bufs, walker, *callbacks, loader_ptr, gpu_mem, false, nullptr, retain);
    registry.update_from_walker(walker, {{root, child}}, empty_bufs, retain);

    selection_result_t selection;
    selection.active_set.insert(root);
    selection.active_set.insert(child);

    std::vector<points_draw_group_t> to_render;
    auto result = emitter.emit(empty_bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    REQUIRE(to_render.size() == 2); // both nodes drawn steady
    REQUIRE(result.points_rendered > 0);

    // Store for next frame
    // We need a persistent bufs vector - create a new scope
  }

  // For the full lifecycle we need persistent state across frames.
  // Restart with persistent variables.
  test_callback_context_t ctx2;
  auto callbacks2 = make_test_callbacks(ctx2);
  draw_emitter_t emitter2;
  frame_node_registry_t registry2;
  gpu_buffer_manager_t mgr2;
  auto loader2 = std::make_unique<mock_node_data_loader_t>();
  std::unique_ptr<node_data_loader_t> loader_ptr2 = std::move(loader2);
  size_t gpu_mem2 = 0;

  // Frame 1: build both nodes
  std::vector<tree_walker_data_t> walker1 = {root_sub, child_sub};
  sort_subsets(walker1);

  std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
  mgr2.reconcile(bufs, walker1, *callbacks2, loader_ptr2, gpu_mem2);
  for (auto &b : bufs)
  {
    make_rendered_steady(*b, ctx2);
    gpu_mem2 += b->gpu_memory_size;
  }

  auto &retain1 = emitter2.prepare_fade_outs(walker1);
  REQUIRE(retain1.empty());

  registry2.update_from_walker(walker1, {{root, child}}, bufs, retain1);

  selection_result_t sel1;
  sel1.active_set.insert(root);
  sel1.active_set.insert(child);

  std::vector<points_draw_group_t> to_render1;
  emitter2.emit(bufs, registry2, sel1, *callbacks2, make_identity_camera(), tree_config, to_render_ptr(to_render1));
  REQUIRE(to_render1.size() == 2);

  // Frame 2: child leaves walker → starts fade-out
  std::vector<tree_walker_data_t> walker2 = {root_sub};
  sort_subsets(walker2);

  auto &retain2 = emitter2.prepare_fade_outs(walker2);
  REQUIRE(retain2.size() == 1);
  REQUIRE(retain2.count(child) == 1);

  // Reconcile with retain — child buffer survives
  mgr2.reconcile(bufs, walker2, *callbacks2, loader_ptr2, gpu_mem2, false, nullptr, retain2);
  REQUIRE(bufs.size() == 2); // both buffers still present

  // Registry with retain — child stays in registry
  registry2.update_from_walker(walker2, {}, bufs, retain2);
  REQUIRE(registry2.get_node(child) != nullptr);

  // Only root in active set (child left the walker)
  selection_result_t sel2;
  sel2.active_set.insert(root);

  // Emit: pass 1 draws root steady, pass 3 draws child fading
  std::vector<points_draw_group_t> to_render2;
  auto result2 = emitter2.emit(bufs, registry2, sel2, *callbacks2, make_identity_camera(), tree_config, to_render_ptr(to_render2));
  REQUIRE(to_render2.size() == 2); // root + fading child
  REQUIRE(result2.any_animating == true);

  // Emit FADE_FRAMES-1 more times to complete the fade-out
  for (int i = 1; i < gpu_node_buffer_t::FADE_FRAMES; i++)
  {
    std::vector<tree_walker_data_t> walker_n = {root_sub};
    sort_subsets(walker_n);

    auto &retain_n = emitter2.prepare_fade_outs(walker_n);
    // Child should still be in retain until fade completes in emit()
    mgr2.reconcile(bufs, walker_n, *callbacks2, loader_ptr2, gpu_mem2, false, nullptr, retain_n);
    registry2.update_from_walker(walker_n, {}, bufs, retain_n);

    selection_result_t sel_n;
    sel_n.active_set.insert(root);

    std::vector<points_draw_group_t> to_render_n;
    emitter2.emit(bufs, registry2, sel_n, *callbacks2, make_identity_camera(), tree_config, to_render_ptr(to_render_n));
  }

  // After FADE_FRAMES, the fade-out entry is removed from m_fading_out.
  // Next prepare_fade_outs should return empty retain (child fully faded).
  {
    std::vector<tree_walker_data_t> walker_final = {root_sub};
    sort_subsets(walker_final);

    auto &retain_final = emitter2.prepare_fade_outs(walker_final);
    REQUIRE(retain_final.empty()); // child fully faded out
  }
}
