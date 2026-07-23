# Read-only point-cloud data library for WebAssembly. Exposes (via embind) an API to open an S3 dataset
# with caller-supplied credentials, load blobs over emscripten_fetch, and return DECOMPRESSED buffers.
# Emits points_data.mjs + points_data.wasm (an ES6 module). Only built under Emscripten.

set(_conv ${CMAKE_CURRENT_SOURCE_DIR}/../converter)
set(_common ${CMAKE_CURRENT_SOURCE_DIR}/../common)

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
)

target_include_directories(points_data_wasm PRIVATE
    ${_conv}
    ${_common}
    ${fmt_SOURCE_DIR}/include
    ${libmorton_SOURCE_DIR}/include
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
