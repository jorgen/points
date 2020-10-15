#ifndef POINTS_RENDERER_H
#define POINTS_RENDERER_H

#include <points/render/points_render_export.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
  namespace render
  {
    enum buffer_type
    {
      buffer_type_vertex,
      buffer_type_uv,
      buffer_type_index,

    };
    enum buffer_format
    {
      U8,
      U16,
      U32,
      R32,
      R64,
    };

    struct buffer_data;
    struct buffer
    {
      enum buffer_type type;
      enum buffer_format format;
      struct buffer_data* data;
      void* user_ptr;
    };

    POINTS_RENDER_EXPORT void buffer_data_add_ref(struct buffer_data* buffer);
    POINTS_RENDER_EXPORT void buffer_data_remove_ref(struct buffer_data* buffer);
    POINTS_RENDER_EXPORT void buffer_data_set_rendered(struct buffer_data* buffer, bool rendered);
    POINTS_RENDER_EXPORT void* buffer_data(struct buffer_data* buffer);
    POINTS_RENDER_EXPORT int buffer_data_size(struct buffer_data* buffer);
    POINTS_RENDER_EXPORT void buffer_set_user_ptr(struct buffer_data*buffer, void* ptr);
    POINTS_RENDER_EXPORT void* buffer_get_user_ptr(struct buffer_data* buffer);

    enum draw_type
    {
      colored_triangle_mesh
    };

    struct draw_group 
    {
      float origin[3];
      struct buffer* buffers;
      int size;
    };

    struct frame
    {
      struct buffer* to_add;
      int to_add_size;
      struct buffer* to_remove;
      int to_remove_size;
      struct draw_group* to_render;
      int to_render_size;
    };

    typedef void (*renderer_dirty_callback)(struct renderer* renderer, void* user_ptr);
    struct renderer;
    POINTS_RENDER_EXPORT struct renderer* renderer_create(const char *url, int url_size);
    POINTS_RENDER_EXPORT void renderer_destroy(struct renderer *renderer);
    POINTS_RENDER_EXPORT void renderer_add_camera(struct renderer* renderer, struct camera* camera);
    POINTS_RENDER_EXPORT void renderer_remove_camera(struct renderer* renderer, struct camera* camera);
    POINTS_RENDER_EXPORT struct frame renderer_frame(struct renderer* renderer, struct camera* camera);
    POINTS_RENDER_EXPORT void renderer_add_callback(struct renderer* renderer, renderer_dirty_callback callback);
  }
}

#ifdef __cplusplus
}
#endif
#endif //POINTS_RENDERER_H
