//#include <points/render/renderer.h>
//#include <points/render/camera.h>

#include "renderer_p.h"

#include <vector>
#include <fmt/printf.h>

#include "camera_p.h"
#include "data_source_p.h"
#include "renderer_callbacks_p.h"

namespace points
{
namespace render
{
struct renderer_t* renderer_create()
{
  auto renderer = new struct renderer_t();
  return renderer;
}
void renderer_destroy(struct renderer_t *renderer)
{
  delete renderer;
}
void renderer_add_camera(struct renderer_t* renderer, struct camera_t* camera)
{
  renderer->cameras.push_back(camera);
}
void renderer_remove_camera(struct renderer_t* renderer, struct camera_t* camera)
{
  auto& cams = renderer->cameras;
  cams.erase(std::remove(cams.begin(), cams.end(), camera), cams.end());
}

struct frame_t renderer_frame(struct renderer_t* renderer, struct camera_t* camera)
{
  frame_camera_t frame_camera;
  frame_camera.view = camera->view;
  frame_camera.projection = camera->projection;
  frame_camera.view_projection = frame_camera.projection * frame_camera.view;
  frame_camera.inverse_view = glm::inverse(frame_camera.view);
  frame_camera.inverse_projection = glm::inverse(frame_camera.projection);
  frame_camera.inverse_view_projection = glm::inverse(frame_camera.view_projection);
  renderer->to_render.clear();
  for (auto &data_source : renderer->data_sources)
  {
    data_source->add_to_frame(frame_camera, renderer->to_render);
  }
  frame_t ret;
  ret.to_render = renderer->to_render.data();
  ret.to_render_size = int(renderer->to_render.size());
  return ret;
}

void renderer_set_callback(struct renderer_t* renderer, renderer_callbacks_t callbacks, void *user_ptr)
{
  renderer->callbacks.set_callbacks(callbacks, user_ptr);
}

void renderer_add_data_source(struct renderer_t *renderer, struct data_source_t *data_source)
{
  auto it = std::find(renderer->data_sources.begin(), renderer->data_sources.end(), data_source);
  if (it == renderer->data_sources.end())
    renderer->data_sources.emplace_back(data_source);
}

void renderer_remove_data_source(struct renderer_t* renderer, struct data_source_t* data_source)
{
  auto it = std::find(renderer->data_sources.begin(), renderer->data_sources.end(), data_source);
  if (it != renderer->data_sources.end())
    renderer->data_sources.erase(it);
}
}
}
