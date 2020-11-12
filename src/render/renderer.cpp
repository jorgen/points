#include "points/render/renderer.h"
#include <vector>
#include <fmt/printf.h>

namespace points
{
  namespace render
  {
    struct renderer
    {
      std::vector<struct camera*> cameras;
      renderer_dirty_callback callback = nullptr;
      aabb aabb;
    };

    struct renderer* renderer_create(const char* url, int url_size)
    {
      (void)url; (void)url_size;
      auto renderer = new struct renderer();
      renderer->aabb.min[0] = 0.0f;
      renderer->aabb.min[1] = 0.0f;
      renderer->aabb.min[2] = 0.0f;
      renderer->aabb.max[0] = 1.0f;
      renderer->aabb.max[1] = 1.0f;
      renderer->aabb.max[2] = 1.0f;
      return new struct renderer();
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
      (void)renderer; (void)camera;
      return {};
    }

    void renderer_add_callback(struct renderer *renderer, renderer_dirty_callback callback)
    {
      renderer->callback = callback;
    }

    struct aabb renderer_aabb(struct renderer* renderer)
    {
      return renderer->aabb;
    }

  }
}
