#ifndef INTERNAL_GLM_INCLUDE_H
#define INTERNAL_GLM_INCLUDE_H

#pragma warning(push)
#pragma warning(disable : 4201 )
#pragma warning(disable : 4127 )  
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include <array>

template<size_t SIZE>
glm::vec<SIZE, glm::f64, glm::defaultp> to_glm(const std::array<double, SIZE> &a)
{
  glm::vec<SIZE, glm::f64, glm::defaultp> ret;
  static_assert(sizeof(ret) == sizeof(a));
  memcpy(&ret, &a, sizeof ret);
  return ret;
}

#endif
