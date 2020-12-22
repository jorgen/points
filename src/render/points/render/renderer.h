#ifndef POINTS_RENDERER_H
#define POINTS_RENDERER_H

#include <points/render/export.h>
#include <points/render/aabb.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
  namespace render
  {
    enum buffer_type
    {
      buffer_type_render_type,
      buffer_type_vertex,
      buffer_type_uv,
      buffer_type_index,
    };
    enum buffer_format
    {
      u8,
      u16,
      u32,
      u64,
      r32,
      r64,
    };

    enum buffer_components
    {
      component_1 = 1,
      component_2 = 2,
      component_3 = 3,
      component_4 = 4
    };

    enum buffer_data_normalize
    {
      do_not_normalize,
      normalize
    };

    struct buffer_data;
    struct buffer
    {
      enum buffer_type type;
      enum buffer_format format;
      enum buffer_components components;
      enum buffer_data_normalize normalize;
      int buffer_mapping;
      struct buffer_data *data;
      void **user_ptr;
    };

    POINTS_RENDER_EXPORT void buffer_data_remove_ref(struct buffer_data *buffer);
    POINTS_RENDER_EXPORT void buffer_data_set_rendered(struct buffer_data *buffer);
    POINTS_RENDER_EXPORT const void *buffer_data_get(struct buffer_data *buffer);
    POINTS_RENDER_EXPORT int buffer_data_size(struct buffer_data *buffer);
    POINTS_RENDER_EXPORT int buffer_data_offset(struct buffer_data *buffer);


    enum draw_type
    {
      aabb_triangle_mesh 
    };

    enum aabb_triangle_mesh_buffer_mapping
    {
      aabb_triangle_mesh_color,
      aabb_triangle_mesh_position
    };

    struct draw_group 
    {
      float origin[3];
      draw_type draw_type;
      struct buffer* buffers;
      int buffers_size;
      int draw_size;
    };

    struct frame
    {
      struct buffer* to_add;
      int to_add_size;
      struct buffer *to_update;
      int to_update_size;
      struct buffer* to_remove;
      int to_remove_size;
      struct draw_group* to_render;
      int to_render_size;
    };

    typedef void (*renderer_dirty_callback)(struct renderer* renderer, void* user_ptr);
    struct renderer;
    struct data_source;
    POINTS_RENDER_EXPORT struct renderer* renderer_create();
    POINTS_RENDER_EXPORT void renderer_destroy(struct renderer *renderer);
    POINTS_RENDER_EXPORT void renderer_add_camera(struct renderer* renderer, struct camera* camera);
    POINTS_RENDER_EXPORT void renderer_remove_camera(struct renderer* renderer, struct camera* camera);
    POINTS_RENDER_EXPORT struct frame renderer_frame(struct renderer* renderer, struct camera* camera);
    POINTS_RENDER_EXPORT void renderer_add_callback(struct renderer* renderer, renderer_dirty_callback callback);
    POINTS_RENDER_EXPORT struct aabb renderer_aabb(struct renderer* renderer);
    POINTS_RENDER_EXPORT void renderer_add_data_source(struct renderer *renderer, struct data_source *data_source);
    POINTS_RENDER_EXPORT void renderer_remove_data_source(struct renderer *renderer, struct data_source *data_source);
  }
}

#ifdef __cplusplus
}
#endif
#endif //POINTS_RENDERER_H
