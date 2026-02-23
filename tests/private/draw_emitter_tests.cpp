#include <catch2/catch.hpp>
#include <draw_emitter.hpp>
#include <gpu_buffer_manager.hpp>
#include <gpu_node_buffer.hpp>
#include <frame_node_registry.hpp>
#include <node_selector.hpp>
#include <renderer_callbacks.hpp>
#include <node_data_loader.hpp>

#include <unordered_map>
#include <unordered_set>

using namespace points;
using namespace points::converter;

using render::callback_manager_t;
using render::node_data_loader_t;
using render::loaded_node_data_t;
using render::load_handle_t;
using render::invalid_load_handle;
using render::to_render_t;
using render::frame_camera_cpp_t;
using render::draw_group_t;
using render::draw_type_t;
using render::dyn_points_1;
using render::dyn_points_3;
using render::dyn_points_crossfade;
using render::dyn_points_bm_vertex;
using render::dyn_points_bm_color;
using render::dyn_points_bm_camera;
using render::dyn_points_bm_old_color;
using render::dyn_points_bm_params;
using render::buffer_type_t;
using render::buffer_type_vertex;
using render::buffer_type_uniform;
using render::renderer_callbacks_t;

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

static void test_create_buffer(render::renderer_t *, void *, buffer_type_t, void **buffer_user_ptr)
{
  g_test_ctx->counter++;
  *buffer_user_ptr = reinterpret_cast<void *>(g_test_ctx->counter);
}

static void test_initialize_buffer(render::renderer_t *, void *, render::buffer_t *, void *, type_t, components_t, int, void *)
{
}

static void test_modify_buffer(render::renderer_t *, void *, render::buffer_t *, void *, int, int, void *)
{
  g_test_ctx->modify_count++;
}

static void test_destroy_buffer(render::renderer_t *, void *, void *buffer_user_ptr)
{
  g_test_ctx->destroyed_buffers.push_back(buffer_user_ptr);
}

static std::unique_ptr<callback_manager_t> make_test_callbacks(test_callback_context_t &ctx)
{
  g_test_ctx = &ctx;
  auto cbm = std::make_unique<callback_manager_t>(nullptr);
  renderer_callbacks_t cbs = {};
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

static to_render_t *to_render_ptr(std::vector<draw_group_t> &vec)
{
  return reinterpret_cast<to_render_t *>(&vec);
}

static void make_rendered_steady(gpu_node_buffer_t &buf, test_callback_context_t &ctx)
{
  buf.rendered = true;
  buf.fade_frame = gpu_node_buffer_t::FADE_FRAMES;
  buf.draw_type = dyn_points_3;
  buf.point_count = 100;

  buf.render_buffers[0].user_ptr = reinterpret_cast<void *>(++ctx.counter); // vertex
  buf.render_buffers[1].user_ptr = reinterpret_cast<void *>(++ctx.counter); // color
  buf.render_buffers[2].user_ptr = reinterpret_cast<void *>(++ctx.counter); // camera

  buf.render_list[0] = {dyn_points_bm_vertex, buf.render_buffers[0].user_ptr};
  buf.render_list[1] = {dyn_points_bm_color, buf.render_buffers[1].user_ptr};
  buf.render_list[2] = {dyn_points_bm_camera, buf.render_buffers[2].user_ptr};

  buf.old_color_valid = false;
  buf.awaiting_new_color = false;
  buf.crossfade_frame = 0;
  buf.params_buffer = {};
  buf.gpu_memory_size = 1024;
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

TEST_CASE("handle_attribute_change", "[gpu_buffer_manager]")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  auto loader = std::make_unique<mock_node_data_loader_t>();
  auto *mock_loader = static_cast<mock_node_data_loader_t *>(loader.get());
  std::unique_ptr<node_data_loader_t> loader_ptr = std::move(loader);
  gpu_buffer_manager_t mgr;
  size_t gpu_mem = 0;

  SECTION("saves current color to old_color_buffer")
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

  SECTION("already-awaiting buffer keeps existing old_color")
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

  SECTION("crossfading buffer gets old_color destroyed then replaced")
  {
    std::vector<std::unique_ptr<gpu_node_buffer_t>> bufs;
    auto b = std::make_unique<gpu_node_buffer_t>();
    make_rendered_steady(*b, ctx);
    // Set up crossfade state: old_color_valid=true, awaiting=false
    void *prev_old_color = reinterpret_cast<void *>(++ctx.counter);
    b->old_color_buffer.user_ptr = prev_old_color;
    b->old_color_valid = true;
    b->awaiting_new_color = false;
    void *current_color = b->render_buffers[1].user_ptr;
    gpu_mem = b->gpu_memory_size;
    bufs.push_back(std::move(b));

    mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);

    // Previous old_color was destroyed
    REQUIRE(std::find(ctx.destroyed_buffers.begin(), ctx.destroyed_buffers.end(), prev_old_color) != ctx.destroyed_buffers.end());
    // New old_color is the current color buffer
    REQUIRE(bufs[0]->old_color_buffer.user_ptr == current_color);
    REQUIRE(bufs[0]->old_color_valid == true);
    REQUIRE(bufs[0]->awaiting_new_color == true);
    REQUIRE(bufs[0]->render_buffers[1].user_ptr == nullptr);
  }

  SECTION("cancels pending load handles")
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

  SECTION("non-rendered buffer gets destroyed entirely")
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

TEST_CASE("draw_emitter steady state emits 3-buffer draw group", "[draw_emitter]")
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

  std::vector<draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.size() == 1);
  REQUIRE(to_render[0].draw_type == dyn_points_3);
  REQUIRE(to_render[0].buffers_size == 3);
  REQUIRE(to_render[0].draw_size == 100);
  REQUIRE(to_render[0].buffers[0].buffer_mapping == dyn_points_bm_vertex);
  REQUIRE(to_render[0].buffers[0].user_ptr == vertex_ptr);
  REQUIRE(to_render[0].buffers[1].buffer_mapping == dyn_points_bm_color);
  REQUIRE(to_render[0].buffers[1].user_ptr == color_ptr);
  REQUIRE(to_render[0].buffers[2].buffer_mapping == dyn_points_bm_camera);
  REQUIRE(to_render[0].buffers[2].user_ptr == camera_ptr);
  REQUIRE(result.any_animating == false);
}

TEST_CASE("draw_emitter fade-in emits crossfade with incrementing alpha", "[draw_emitter]")
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
  std::vector<draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.size() == 1);
  REQUIRE(to_render[0].draw_type == dyn_points_crossfade);
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
  REQUIRE(to_render[0].draw_type == dyn_points_3);
  REQUIRE(to_render[0].buffers_size == 3);
  REQUIRE(result.any_animating == false);
}

TEST_CASE("draw_emitter awaiting state uses old_color_buffer for both color slots", "[draw_emitter]")
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

  std::vector<draw_group_t> to_render;
  auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(to_render.size() == 1);
  REQUIRE(to_render[0].draw_type == dyn_points_crossfade);
  REQUIRE(to_render[0].buffers_size == 5);
  REQUIRE(result.any_animating == true);

  // Both color and old_color should point to old_color_buffer
  REQUIRE(to_render[0].buffers[1].buffer_mapping == dyn_points_bm_color);
  REQUIRE(to_render[0].buffers[1].user_ptr == old_color_ptr);
  REQUIRE(to_render[0].buffers[3].buffer_mapping == dyn_points_bm_old_color);
  REQUIRE(to_render[0].buffers[3].user_ptr == old_color_ptr);

  // blend should be 1.0 (params_data.y)
  REQUIRE(bufs[0]->params_data.y == Approx(1.0f));
}

TEST_CASE("draw_emitter crossfade progresses when parent is not transitioning", "[draw_emitter]")
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
    std::vector<draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
  }

  // After 10 frames, crossfade should be complete
  REQUIRE(bufs[1]->crossfade_frame == 10);
  REQUIRE(bufs[1]->old_color_valid == false);
}

TEST_CASE("draw_emitter crossfade blocked when parent is transitioning", "[draw_emitter]")
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
  std::vector<draw_group_t> to_render;
  emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

  REQUIRE(bufs[1]->crossfade_frame == 0);
  REQUIRE(bufs[1]->params_data.y == Approx(0.0f));
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

TEST_CASE("draw_emitter skips non-visible and non-rendered nodes", "[draw_emitter]")
{
  test_callback_context_t ctx;
  auto callbacks = make_test_callbacks(ctx);
  draw_emitter_t emitter;
  frame_node_registry_t registry;
  tree_config_t tree_config = {};

  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  SECTION("non-visible node produces no draw groups")
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

    std::vector<draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    REQUIRE(to_render.empty());
  }

  SECTION("non-rendered node produces no draw groups")
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

    std::vector<draw_group_t> to_render;
    emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));

    REQUIRE(to_render.empty());
  }
}

// ---------------------------------------------------------------------------
// Lifecycle / regression tests
// ---------------------------------------------------------------------------

TEST_CASE("full lifecycle: steady -> attribute change -> awaiting -> crossfade -> steady", "[draw_emitter][gpu_buffer_manager]")
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
    std::vector<draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == dyn_points_3);
    REQUIRE(result.any_animating == false);
  }

  // Phase 2: attribute change -> awaiting
  mgr.handle_attribute_change(bufs, *callbacks, loader_ptr, gpu_mem);
  REQUIRE(bufs[0]->awaiting_new_color == true);
  REQUIRE(bufs[0]->old_color_buffer.user_ptr == original_color);

  // Phase 3: emit while awaiting (old color shown for both slots)
  {
    std::vector<draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == dyn_points_crossfade);
    REQUIRE(result.any_animating == true);
    REQUIRE(to_render[0].buffers[1].user_ptr == original_color);
    REQUIRE(to_render[0].buffers[3].user_ptr == original_color);
  }

  // Phase 4: simulate new color data arrival
  // (In production this happens via upload_ready, but we manually set the state)
  void *new_color = reinterpret_cast<void *>(++ctx.counter);
  bufs[0]->render_buffers[1].user_ptr = new_color;
  bufs[0]->awaiting_new_color = false;
  bufs[0]->crossfade_frame = 0;
  // old_color_valid stays true to trigger crossfade

  // Phase 5: emit through crossfade (10 frames)
  for (int i = 0; i < 10; i++)
  {
    std::vector<draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == dyn_points_crossfade);
    REQUIRE(result.any_animating == true);
  }

  // Phase 6: next emit should be back to steady state
  REQUIRE(bufs[0]->old_color_valid == false);
  {
    std::vector<draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].draw_type == dyn_points_3);
    REQUIRE(to_render[0].buffers_size == 3);
    REQUIRE(result.any_animating == false);
    REQUIRE(to_render[0].buffers[1].user_ptr == new_color);
  }
}

TEST_CASE("rapid double attribute change preserves old_color", "[draw_emitter][gpu_buffer_manager]")
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
    std::vector<draw_group_t> to_render;
    auto result = emitter.emit(bufs, registry, selection, *callbacks, make_identity_camera(), tree_config, to_render_ptr(to_render));
    REQUIRE(to_render[0].buffers[1].user_ptr == color_a);
    REQUIRE(to_render[0].buffers[3].user_ptr == color_a);
    REQUIRE(result.any_animating == true);
  }
}

TEST_CASE("color crossfade does not block LOD fade_complete", "[gpu_buffer_manager]")
{
  // Regression: old_color_valid and awaiting_new_color used to set
  // all_fade_complete=false, blocking parent->child LOD swap in the
  // selector and causing 10x refine slowdown.
  auto root = make_nid(1, 0, 0);
  node_id_t empty = {};

  std::vector<tree_walker_data_t> subsets = {make_sub(root, empty, 10)};

  SECTION("awaiting_new_color does not prevent all_fade_complete")
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

  SECTION("old_color_valid does not prevent all_fade_complete")
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

  SECTION("incomplete LOD fade still blocks all_fade_complete")
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

TEST_CASE("child crossfade not blocked by non-active parent", "[draw_emitter]")
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
  std::vector<draw_group_t> to_render;
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

TEST_CASE("upload_ready processes root node whose parent equals empty_node_id", "[gpu_buffer_manager]")
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
  root_data.attribute_type = type_u16;
  root_data.attribute_components = components_3;
  root_data.point_count = 1;
  root_data.draw_type = dyn_points_3;
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
  child_data.attribute_type = type_u16;
  child_data.attribute_components = components_3;
  child_data.point_count = 1;
  child_data.draw_type = dyn_points_3;
  mock_loader->loaded_data[child_handle] = child_data;

  gpu_mem += child_buf->gpu_memory_size;
  bufs.push_back(std::move(child_buf));

  // Call upload_ready — both nodes should be processed
  int uploads = mgr.upload_ready(bufs, *callbacks, loader_ptr, gpu_mem,
                                  gpu_mem + 1024 * 1024, 10, make_identity_camera(),
                                  "intensity", 0.0, 1.0);

  REQUIRE(uploads == 2);
  REQUIRE(bufs[0]->awaiting_new_color == false);
  REQUIRE(bufs[0]->load_handle == render::invalid_load_handle);
  REQUIRE(bufs[1]->awaiting_new_color == false);
  REQUIRE(bufs[1]->load_handle == render::invalid_load_handle);
}
