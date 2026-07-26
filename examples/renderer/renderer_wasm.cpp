// WebGL2/WASM entry point for the point-cloud renderer. The React (JS) side owns the <canvas>, the DOM
// event handling, and the animation loop; this module owns the WebGL2 rendering (the desktop gl_renderer,
// ported to GLES3), the octree stream/decode pipeline, and the arcball camera math.
//
// Rendering is DIRTY-DRIVEN, not continuous: the module never installs a self-driving main loop. Instead
// it calls a JS `requestUpdate` callback whenever the frame would change (camera input, or an async
// storage read completing -- surfaced through vio::wasm's wake hook). JS coalesces those into a single
// requestAnimationFrame -> frame(), which pumps the cooperative loops and draws once, then goes idle.

#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include "gl_renderer.h"

#include <points/render/aabb.h>
#include <points/render/camera.h>
#include <points/render/renderer.h>

#include <points/converter/converter_data_source.h>

#include "error.hpp" // the full points_error_t (code + std::string msg)

#include <vio/objstore/create_object_store.h> // parse_connection_string, apply_connection_override
#include <vio/platform/wasm/event_loop_impl.h> // vio::wasm::pump / set_wake_hook

#include <cstring>
#include <memory>
#include <string>

using namespace emscripten;

// A single renderer instance: GL context + render object graph + streaming data source + arcball camera.
// Constructed by create_renderer() (which does the async open) and handed to JS as an embind object.
class renderer_wasm_t
{
public:
  renderer_wasm_t() = default;

  ~renderer_wasm_t()
  {
    dispose();
  }

  // Release GL + data-source resources. Idempotent, so React can call it on unmount (and under
  // StrictMode's mount/unmount/mount cycle) to free the WebGL context immediately instead of waiting for
  // GC. After dispose() the instance must not be used again.
  void dispose()
  {
    // Detach the wake hook first so a late fetch completion cannot call into a disposed instance -- but
    // only if WE still own it. The hook is process-global; a newer instance (e.g. React StrictMode's
    // overlapping create/dispose) may have taken it over, and we must not clear that one.
    if (wake_owner() == this)
    {
      vio::wasm::set_wake_hook({});
      wake_owner() = nullptr;
    }
    _request_update = val::undefined();
    if (_cds)
    {
      points_converter_data_source_destroy(_cds);
      _cds = nullptr;
    }
    // renderer/camera/arcball are owned by the render library; gl_renderer holds only non-owning refs.
    if (_gl_ctx)
    {
      emscripten_webgl_destroy_context(_gl_ctx);
      _gl_ctx = 0;
    }
  }

  // JS registers the redraw callback. Also wires vio's wake hook (fired when an async storage read
  // completes outside a frame) to it, so a fetch landing schedules exactly one redraw.
  void setRequestUpdate(val cb)
  {
    _request_update = std::move(cb);
    wake_owner() = this;
    vio::wasm::set_wake_hook([this]() { mark_dirty(); });
  }

  // Pump the cooperative loops (running any completed-read resumes + issuing new loads), then draw once.
  // Called from JS inside a requestAnimationFrame. w/h are the drawing-buffer pixel size.
  void frame(int w, int h)
  {
    emscripten_webgl_make_context_current(_gl_ctx);
    if (w != _width || h != _height)
      apply_size(w, h);
    vio::wasm::pump();
    _gl->draw(static_cast<clear>(int(clear::color) | int(clear::depth)), _width, _height);
  }

  // Semantic camera ops. Inputs are normalized deltas (React divides pixel movement by canvas size); the
  // arcball owns the actual math. Each marks the frame dirty so JS schedules a redraw.
  void cameraRotate(float ndx, float ndy)
  {
    points_arcball_rotate(_arcball, ndx, ndy, 0.0f);
    mark_dirty();
  }
  void cameraRoll(float nd)
  {
    points_arcball_rotate(_arcball, 0.0f, 0.0f, nd);
    mark_dirty();
  }
  void cameraPan(float ndx, float ndy)
  {
    points_arcball_pan(_arcball, ndx, ndy);
    mark_dirty();
  }
  void cameraDolly(float nd)
  {
    points_arcball_dolly(_arcball, nd);
    mark_dirty();
  }
  void cameraZoom(float nz)
  {
    points_arcball_zoom(_arcball, nz);
    mark_dirty();
  }

  void setAttribute(const std::string &name)
  {
    points_converter_data_set_rendered_attribute(_cds, name.c_str(), uint32_t(name.size()));
    mark_dirty();
  }

  val getAttributeNames()
  {
    val out = val::array();
    uint32_t count = points_converter_data_attribute_count(_cds);
    char buf[256];
    for (uint32_t i = 0; i < count; ++i)
    {
      uint32_t n = points_converter_data_get_attribute_name(_cds, int(i), buf, sizeof(buf));
      size_t len = ::strnlen(buf, n < sizeof(buf) ? n : sizeof(buf)); // the C API may include a trailing NUL
      out.set(i, std::string(buf, buf + len));
    }
    return out;
  }

  val getAabb()
  {
    val mn = val::array();
    val mx = val::array();
    for (int i = 0; i < 3; ++i)
    {
      mn.set(i, _aabb_min[i]);
      mx.set(i, _aabb_max[i]);
    }
    val out = val::object();
    out.set("min", mn);
    out.set("max", mx);
    return out;
  }

  double getPointsRendered()
  {
    return double(points_converter_data_source_get_points_rendered(_cds));
  }

  // --- appearance (gl_renderer public fields) ---
  void setPointSize(float v)
  {
    if (_gl)
      _gl->point_world_size = v;
    mark_dirty();
  }
  void setLodScaleBase(float v)
  {
    if (_gl)
      _gl->lod_scale_base = v;
    mark_dirty();
  }

  // --- streaming / level of detail (data-source tuning) ---
  // Octree refinement budget: a smaller screen-space pixel error draws more detail (and streams more).
  void setPixelErrorThreshold(double v)
  {
    points_converter_data_source_set_pixel_error_threshold(_cds, v);
    mark_dirty();
  }
  // GPU memory budget in MB; the streamer evicts to stay under it.
  void setGpuMemoryBudgetMb(double mb)
  {
    if (mb < 0.0)
      mb = 0.0;
    points_converter_data_source_set_gpu_memory_budget(_cds, static_cast<size_t>(mb) * 1024u * 1024u);
    mark_dirty();
  }

  // --- scene overlays ---
  void setShowBoundingBoxes(bool show)
  {
    points_converter_data_source_set_show_bounding_boxes(_cds, show ? 1 : 0);
    mark_dirty();
  }

  // --- camera ---
  // Pan within the dataset's ground plane (the desktop app's ctrl+right-drag gesture).
  void cameraPanGround(float ndx, float ndy)
  {
    points_arcball_pan_ground(_arcball, ndx, ndy);
    mark_dirty();
  }
  // Restore the initial fitted view (the arcball was created at the AABB-fit camera, so reset returns to it).
  void resetView()
  {
    points_arcball_reset(_arcball);
    mark_dirty();
  }

private:
  friend renderer_wasm_t *create_renderer(std::string, std::string, std::string);

  void mark_dirty()
  {
    if (!_request_update.isUndefined() && !_request_update.isNull())
      _request_update();
  }

  // Which instance currently owns the single process-global vio wake hook (see setRequestUpdate/dispose).
  static renderer_wasm_t *&wake_owner()
  {
    static renderer_wasm_t *owner = nullptr;
    return owner;
  }

  void apply_size(int w, int h)
  {
    _width = w;
    _height = h;
    points_camera_set_perspective(_camera, 45.0, double(w), double(h), 0.1, 100000.0);
    points_converter_data_source_set_viewport(_cds, w, h);
  }

  static void on_aabb(double amin[3], double amax[3], void *user)
  {
    auto *self = static_cast<renderer_wasm_t *>(user);
    std::memcpy(self->_aabb_min, amin, 3 * sizeof(double));
    std::memcpy(self->_aabb_max, amax, 3 * sizeof(double));
    self->_aabb_ready = true;
  }

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE _gl_ctx = 0;
  points_renderer_t *_renderer = nullptr;
  points_camera_t *_camera = nullptr;
  std::unique_ptr<gl_renderer> _gl;
  points_converter_data_source_t *_cds = nullptr;
  points_arcball_t *_arcball = nullptr;
  int _width = 0;
  int _height = 0;
  val _request_update = val::undefined();
  bool _aabb_ready = false;
  double _aabb_min[3] = {0, 0, 0};
  double _aabb_max[3] = {0, 0, 0};
};

// Async factory (suspends via Asyncify while the root tree + AABB load, so JS gets a Promise<Renderer>).
// canvas_selector is a CSS selector for the React-owned <canvas> (e.g. "#points-canvas"). url is the dataset
// location -- scheme + bucket/prefix, e.g. "s3://bucket/prefix". connection_string is a vio connection
// string (the SAME grammar and keys the CLI tools use, minus the URL) carrying the remaining connection
// parameters, e.g. "endpoint=https://host:9000;access_key_id=..;secret_access_key=..;path_style=true".
renderer_wasm_t *create_renderer(std::string canvas_selector, std::string url, std::string connection_string)
{
  auto *r = new renderer_wasm_t();

  // 1. WebGL2 context on the React-owned canvas. preserveDrawingBuffer=false is correct for on-demand
  //    rendering -- the compositor retains the last drawn frame between redraws.
  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.alpha = false;
  attrs.depth = true;
  attrs.stencil = false;
  attrs.antialias = true;
  attrs.preserveDrawingBuffer = false;
  r->_gl_ctx = emscripten_webgl_create_context(canvas_selector.c_str(), &attrs);
  if (r->_gl_ctx <= 0)
  {
    emscripten_console_error("createRenderer: failed to create a WebGL2 context on the canvas selector");
    delete r;
    return nullptr;
  }
  emscripten_webgl_make_context_current(r->_gl_ctx);

  // 2. Resolve + install the credentials for the dataset's provider from the connection string (the 'url'
  //    key is ignored here). getenv is meaningless in the browser, so the credentials must be in the string.
  if (auto applied = vio::objstore::apply_connection_override(url, connection_string); !applied)
  {
    emscripten_console_error(("createRenderer: " + applied.error().msg).c_str());
    delete r;
    return nullptr;
  }

  // 3. Render object graph + streaming data source. data_source_create opens the dataset, which drives
  //    the (Asyncify) busy-yield in request_root -- this call suspends until the root tree is loaded.
  r->_renderer = points_renderer_create();
  r->_camera = points_camera_create();
  r->_gl = std::make_unique<gl_renderer>(r->_renderer, r->_camera);

  points_error_t err{};
  r->_cds = points_converter_data_source_create(url.c_str(), uint32_t(url.size()), &err, r->_renderer);
  if (err.code != 0 || !r->_cds)
  {
    emscripten_console_error(err.msg.empty() ? "createRenderer: failed to open dataset" : err.msg.c_str());
    delete r;
    return nullptr;
  }
  points_renderer_add_data_source(r->_renderer, points_converter_data_source_get(r->_cds));

  // 4. Fit the camera to the dataset AABB (request is async; suspend until the callback fires).
  points_converter_data_source_request_aabb(r->_cds, &renderer_wasm_t::on_aabb, r);
  while (!r->_aabb_ready)
  {
    vio::wasm::pump();
    emscripten_sleep(0);
  }

  // 5. Perspective + arcball. Default to a Z-up dataset viewed from -Y (matches the desktop example).
  const int w0 = 1, h0 = 1; // real size arrives with the first frame() call
  r->apply_size(w0, h0);
  points_aabb_t aabb;
  std::memcpy(aabb.min, r->_aabb_min, 3 * sizeof(double));
  std::memcpy(aabb.max, r->_aabb_max, 3 * sizeof(double));
  const double dir[3] = {0.0, -1.0, 0.0};
  const double up[3] = {0.0, 0.0, 1.0};
  points_camera_look_at_aabb(r->_camera, &aabb, dir, up);
  const double center[3] = {(r->_aabb_min[0] + r->_aabb_max[0]) * 0.5, (r->_aabb_min[1] + r->_aabb_max[1]) * 0.5, (r->_aabb_min[2] + r->_aabb_max[2]) * 0.5};
  r->_arcball = points_arcball_create(r->_camera, center);
  points_arcball_set_up_axis(r->_arcball, up);

  return r;
}

EMSCRIPTEN_BINDINGS(points_render)
{
  class_<renderer_wasm_t>("Renderer")
    .function("setRequestUpdate", &renderer_wasm_t::setRequestUpdate)
    .function("frame", &renderer_wasm_t::frame)
    .function("cameraRotate", &renderer_wasm_t::cameraRotate)
    .function("cameraRoll", &renderer_wasm_t::cameraRoll)
    .function("cameraPan", &renderer_wasm_t::cameraPan)
    .function("cameraDolly", &renderer_wasm_t::cameraDolly)
    .function("cameraZoom", &renderer_wasm_t::cameraZoom)
    .function("cameraPanGround", &renderer_wasm_t::cameraPanGround)
    .function("resetView", &renderer_wasm_t::resetView)
    .function("setAttribute", &renderer_wasm_t::setAttribute)
    .function("getAttributeNames", &renderer_wasm_t::getAttributeNames)
    .function("getAabb", &renderer_wasm_t::getAabb)
    .function("getPointsRendered", &renderer_wasm_t::getPointsRendered)
    .function("setPointSize", &renderer_wasm_t::setPointSize)
    .function("setLodScaleBase", &renderer_wasm_t::setLodScaleBase)
    .function("setPixelErrorThreshold", &renderer_wasm_t::setPixelErrorThreshold)
    .function("setGpuMemoryBudgetMb", &renderer_wasm_t::setGpuMemoryBudgetMb)
    .function("setShowBoundingBoxes", &renderer_wasm_t::setShowBoundingBoxes)
    .function("dispose", &renderer_wasm_t::dispose);

  function("createRenderer", &create_renderer, allow_raw_pointers());
}
