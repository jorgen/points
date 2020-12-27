#include <aabb_p.h>
#include <points/render/camera.h>
#include <points/render/renderer.h>
#include "data_source_p.h"
#include "glm_include.h"
#include "buffer_data_p.h"

#include <vector>
#include <memory>
namespace points
{
namespace render
{

struct aabb_buffer
{
  aabb aabb;
  std::vector<glm::vec3> vertices;
  std::unique_ptr<buffer_data> vertices_buffer_data;

  buffer render_list[3];
};


struct aabb_data_source : public data_source
{
  aabb_data_source();

  void add_to_frame(const renderer &renderer, const camera &camera, std::vector<buffer> &to_add, std::vector<buffer> &to_update, std::vector<buffer> &to_remove, std::vector<draw_group> &to_render) override;

  std::vector<aabb_buffer> aabbs;
  std::vector<std::pair<buffer, buffer_data>> to_remove_buffers;

  std::vector<uint32_t> aabbs_ids;

  buffer index_buffer;
  std::unique_ptr<buffer_data> index_buffer_data;
  std::vector<uint16_t> indecies;

  buffer color_buffer;
  std::unique_ptr<buffer_data> color_buffer_data;
  std::vector<glm::u8vec3> colors;

};
} // namespace render
} // namespace points
