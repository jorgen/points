#include "points/render/renderer.h"
#include <vector>
#include <fmt/printf.h>

#include "data_source_p.h"
namespace points
{
  namespace render
  {
    struct renderer
    {
      std::vector<camera*> cameras;
      std::vector<data_source *> data_sources;
      std::vector<buffer> to_add;
      std::vector<buffer> to_update;
      std::vector<buffer> to_remove;
      std::vector<draw_group> to_render;
      renderer_dirty_callback callback = nullptr;
      aabb aabb;
    };

    struct renderer* renderer_create()
    {
      auto renderer = new struct renderer();
      renderer->aabb.min[0] = 0.0f;
      renderer->aabb.min[1] = 0.0f;
      renderer->aabb.min[2] = 0.0f;
      renderer->aabb.max[0] = 1.0f;
      renderer->aabb.max[1] = 1.0f;
      renderer->aabb.max[2] = 1.0f;
      return renderer;
    }
    void renderer_destroy(struct renderer *renderer)
    {
      delete renderer;
    }
    void renderer_add_camera(struct renderer* renderer, struct camera* camera)
    {
      renderer->cameras.push_back(camera);
    }
    void renderer_remove_camera(struct renderer* renderer, struct camera* camera)
    {
      auto& cams = renderer->cameras;
      cams.erase(std::remove(cams.begin(), cams.end(), camera), cams.end());
    }

    struct frame renderer_frame(struct renderer* renderer, struct camera* camera)
    {
      renderer->to_add.clear();
      renderer->to_update.clear();
      renderer->to_remove.clear();
      renderer->to_render.clear();
      for (auto &data_source : renderer->data_sources)
      {
        data_source->add_to_frame(*renderer, *camera, renderer->to_add, renderer->to_update, renderer->to_remove, renderer->to_render);
      }
      frame ret;
      ret.to_add = renderer->to_add.data();
      ret.to_add_size = int(renderer->to_add.size());
      ret.to_update = renderer->to_update.data();
      ret.to_update_size = int(renderer->to_update.size());
      ret.to_remove = renderer->to_remove.data();
      ret.to_remove_size = int(renderer->to_remove.size());
      ret.to_render = renderer->to_render.data();
      ret.to_render_size = int(renderer->to_render.size());
      return ret;
    }

    void renderer_add_callback(struct renderer *renderer, renderer_dirty_callback callback)
    {
      renderer->callback = callback;
    }

    struct aabb renderer_aabb(struct renderer* renderer)
    {
      return renderer->aabb;
    }
    
    void renderer_add_data_source(struct renderer *renderer, struct data_source *data_source)
    {
      auto it = std::find(renderer->data_sources.begin(), renderer->data_sources.end(), data_source);
      if (it == renderer->data_sources.end())
        renderer->data_sources.emplace_back(data_source);
    }

    void renderer_remove_data_source(struct renderer* renderer, struct data_source* data_source)
    {
      auto it = std::find(renderer->data_sources.begin(), renderer->data_sources.end(), data_source);
      if (it != renderer->data_sources.end())
        renderer->data_sources.erase(it);
    }
  }
}
