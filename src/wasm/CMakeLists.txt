# Read-only point-cloud data library for WebAssembly. Exposes (via embind) an API to open an S3 dataset
# with caller-supplied credentials, load blobs over emscripten_fetch, and return DECOMPRESSED buffers.
# Emits points_data.mjs + points_data.wasm (an ES6 module). Only built under Emscripten.

set(_conv ${CMAKE_CURRENT_SOURCE_DIR}/../converter)
set(_common ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(_render ${CMAKE_CURRENT_SOURCE_DIR}/../render)

add_executable(points_data_wasm
    points_wasm_api.cpp
    ${_common}/error.cpp
    # The portable read + decompress subset (no libuv / laszip):
    ${_conv}/object_backend.cpp
    ${_conv}/compressor.cpp
    ${_conv}/compressor_zstd.cpp
    ${_conv}/compressor_fse.cpp
    ${_conv}/compressor_ans.cpp
    ${_conv}/byte_shuffle.cpp
    ${_conv}/compression_preprocess.cpp
    # Tree (de)serialize + node storage map + attribute configs, for the frustum-walk / readNode path:
    ${_conv}/tree.cpp
    ${_conv}/input_storage_map.cpp
    ${_conv}/attributes_configs.cpp
)

target_include_directories(points_data_wasm PRIVATE
    ${_conv}
    ${_common}
    ${_render}
    ${fmt_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${libmorton_SOURCE_DIR}/include
    ${unordered_dense_SOURCE_DIR}/include
    ${zstd_SOURCE_DIR}/lib
)

# FMT_HEADER_ONLY avoids building the fmt library under emcc; ZSTD_DISABLE_ASM skips zstd's .S paths.
target_compile_definitions(points_data_wasm PRIVATE FMT_HEADER_ONLY=1 ZSTD_DISABLE_ASM=1)

# embind requires RTTI (the top-level build sets -fno-rtti globally); fmt 11 references malloc/free
# without pulling <cstdlib> under emscripten's clang, so force-include it. Our own code stays
# -fno-exceptions (which is what embind's type registration expects here), but vio is compiled with
# exceptions and references __cxa_throw, so the throwing runtime must still be linked in
# (-sDISABLE_EXCEPTION_THROWING=0) even though we don't throw ourselves.
# --bind is needed at COMPILE time too (not only link) for embind. -fvisibility=default overrides the
# project-wide CXX_VISIBILITY_PRESET=hidden: hidden visibility keeps the per-TU RTTI type_info symbols
# from merging with embind's runtime, which makes every bound function report "unbound types" at call time.
target_compile_options(points_data_wasm PRIVATE -frtti -include cstdlib --bind -fvisibility=default)

target_link_libraries(points_data_wasm PRIVATE vio libzstd_static)

target_link_options(points_data_wasm PRIVATE
    # RTTI must also be on at LINK: emscripten (re)builds its system libs -- including libembind -- with
    # the link-command flags, and the global -fno-rtti would build libembind without RTTI, so its
    # type_info for int/val/etc. would not match our -frtti TUs -> "unbound types" for every function.
    -frtti
    "-sDISABLE_EXCEPTION_THROWING=0"
    --bind
    "-sFETCH=1"
    "-sASYNCIFY"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXIT_RUNTIME=0"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=PointsData"
    "-sENVIRONMENT=web,worker,node"
    "-sSTACK_SIZE=5MB"
)

set_target_properties(points_data_wasm PROPERTIES OUTPUT_NAME "points_data" SUFFIX ".mjs")

# ---------------------------------------------------------------------------------------------------
# WebGL2 renderer library. Streams + decodes the octree and draws it with the desktop gl_renderer
# (ported to GLES3), driven by a React canvas + a semantic arcball camera API. Emits
# points_render.mjs + .wasm (an ES6 module). Rendering is dirty-driven: WASM calls a JS requestUpdate
# callback (input / async-IO completion) instead of a continuous main loop.
set(_examples ${CMAKE_SOURCE_DIR}/examples/renderer)

# The GLSL sources, embedded via cmrc under the "shaders" namespace (matches gl_renderer's CMRC_DECLARE).
# WHENCE keeps the resource names as "shaders/xxx" even though the target lives in src/wasm.
cmrc_add_resource_library(points_render_wasm_shaders ALIAS points_render_wasm::shaders NAMESPACE shaders
    WHENCE ${_examples}
    ${_examples}/shaders/aabb.frag
    ${_examples}/shaders/aabb.vert
    ${_examples}/shaders/points.frag
    ${_examples}/shaders/points.vert
    ${_examples}/shaders/dynpoints.frag
    ${_examples}/shaders/dynpoints_1.vert
    ${_examples}/shaders/dynpoints_3.vert
    ${_examples}/shaders/dynpoints_crossfade.vert
    ${_examples}/shaders/node_bbox.vert
    ${_examples}/shaders/node_bbox.frag
    ${_examples}/shaders/axis_gizmo.vert
    ${_examples}/shaders/axis_gizmo.frag
    ${_examples}/shaders/origin_anchor.vert
    ${_examples}/shaders/origin_anchor.frag
    ${_examples}/shaders/environment.vert
    ${_examples}/shaders/environment.frag
    ${_examples}/shaders/skybox.vert
    ${_examples}/shaders/skybox.frag
)
# cmrc.hpp calls abort() without <cstdlib>; emscripten's strict headers need it force-included (the
# generated cmrc TU does not inherit the main target's -include cstdlib).
target_compile_options(points_render_wasm_shaders PRIVATE -include cstdlib)

add_executable(points_render_wasm
    ${_examples}/renderer_wasm.cpp
    ${_examples}/gl_renderer.cpp
    # points_render (pure C++/glm; no GL, no uv)
    ${_render}/renderer.cpp
    ${_render}/camera.cpp
    ${_render}/data_source.cpp
    ${_render}/data_source_aabb.cpp
    ${_render}/data_source_skybox.cpp
    ${_render}/data_source_axis_gizmo.cpp
    ${_render}/data_source_origin_anchor.cpp
    ${_render}/data_source_environment.cpp
    ${_render}/buffer.cpp
    ${_render}/image_decoder.cpp
    # converter read/stream path (the write/laszip/packed-file TUs are excluded)
    ${_conv}/processor.cpp
    ${_conv}/storage_handler.cpp
    ${_conv}/tree_handler.cpp
    ${_conv}/tree_lod_generator.cpp
    ${_conv}/tree.cpp
    ${_conv}/sorter.cpp
    ${_conv}/reader.cpp
    ${_conv}/input_header.cpp
    ${_conv}/attributes_configs.cpp
    ${_conv}/data_source_converter.cpp
    ${_conv}/data_source_node_bbox.cpp
    ${_conv}/draw_emitter.cpp
    ${_conv}/frame_node_registry.cpp
    ${_conv}/frustum_tree_walker.cpp
    ${_conv}/gpu_buffer_manager.cpp
    ${_conv}/node_selector.cpp
    ${_conv}/native_node_data_loader.cpp
    ${_conv}/blob_manager.cpp
    ${_conv}/input_data_source_registry.cpp
    ${_conv}/input_storage_map.cpp
    ${_conv}/storage_backend.cpp
    ${_conv}/object_backend.cpp
    ${_conv}/compressor.cpp
    ${_conv}/compressor_zstd.cpp
    ${_conv}/compressor_fse.cpp
    ${_conv}/compressor_ans.cpp
    ${_conv}/byte_shuffle.cpp
    ${_conv}/compression_preprocess.cpp
    ${_common}/error.cpp
)

target_include_directories(points_render_wasm PRIVATE
    ${_examples}
    ${_conv}
    ${_common}
    ${_render}
    ${fmt_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${libmorton_SOURCE_DIR}/include
    ${unordered_dense_SOURCE_DIR}/include
    ${zstd_SOURCE_DIR}/lib
    ${stbimage_SOURCE_DIR}
)

target_compile_definitions(points_render_wasm PRIVATE FMT_HEADER_ONLY=1 ZSTD_DISABLE_ASM=1)
target_compile_options(points_render_wasm PRIVATE -frtti -include cstdlib --bind -fvisibility=default)

target_link_libraries(points_render_wasm PRIVATE vio libzstd_static points_render_wasm::shaders)

target_link_options(points_render_wasm PRIVATE
    -frtti
    "-sDISABLE_EXCEPTION_THROWING=0"
    --bind
    "-sFETCH=1"
    "-sASYNCIFY"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXIT_RUNTIME=0"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=PointsRender"
    "-sENVIRONMENT=web,worker,node"
    "-sSTACK_SIZE=8MB"
    # WebGL2 / GLES3 (FULL_ES3 also enables glMapBufferRange + client-array emulation).
    "-sMAX_WEBGL_VERSION=2"
    "-sMIN_WEBGL_VERSION=2"
    "-sFULL_ES3=1"
)

set_target_properties(points_render_wasm PROPERTIES OUTPUT_NAME "points_render" SUFFIX ".mjs")
