set(EXPORTED_HEADERS
  points/error.h
  points/export.h
)

add_library(points_common INTERFACE ${EXPORTED_HEADERS})
set_target_properties(${target}
  PROPERTIES
  PUBLIC_HEADER "${EXPORTED_HEADERS}"
)
target_include_directories(points_common
  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)
