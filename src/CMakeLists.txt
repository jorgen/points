if(EMSCRIPTEN)
    # WebAssembly: build only the read-only data library (it compiles the portable read/decompress
    # subset of converter + core directly). The full converter/render libraries pull in libuv, laszip
    # and windowing code that do not belong in the browser data path.
    add_subdirectory(wasm)
else()
    add_subdirectory(core)
    add_subdirectory(render)
    add_subdirectory(converter)
endif()
