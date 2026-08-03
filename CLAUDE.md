# dewfall

Point cloud processing library (formerly "points"). Converts arbitrary point cloud input into a
morton-ordered octree with LOD (level of detail) stored in a compressed, random-accessible buffer
format (`.dew` files). Includes a graphics-API-agnostic rendering framework with callback-driven
buffer management and an OpenGL example implementation.

C++23, no RTTI, no exceptions. Public API is pure C (C99/C11 compatible), prefixed `dew_`;
internal namespaces are `dew::converter`, `dew::render`, `dew::`.

## Building

```bash
cd /c/Users/jorge/dev/dewfall
cmake --build build --target <target>
```

Key targets:
- `dew_converter` — converter library
- `dew_render` — rendering library
- `dew_common` — shared types library
- `private_interface_unit_tests` — internal unit tests
- `public_interface_unit_tests` — public API unit tests
- `dew` — the dewfall CLI: `dew convert` (full LAS/LAZ converter), `dew info` (dataset stats),
  `dew extract` (octree inspection + attribute extraction), `dew copy` (dataset copy/migration,
  also migrates pre-rename magics), `dew laz` (LAS/LAZ header/VLR/point introspection)
- `renderer_example` — OpenGL renderer example
- `dew_render_wasm` / `dew_data_wasm` / `dew_decode_worker` — Emscripten modules (src/wasm)
- `dew_python` — the `dew` Python extension (only with `-DDEW_BUILD_PYTHON=ON`, off by default;
  needs a python3 with `pip install libclang`). Generated at build time from the public headers
  by `tools/bindgen/`: `parse_headers.py` (libclang → `dew_api.json`) then `generate_nanobind.py`
  (JSON → nanobind C++). `//=` comment tags in the headers steer both stages; run
  `parse_headers.py --check` to lint the surface. Packaged as abi3 wheels via `pyproject.toml`
  (scikit-build-core) — see the Python bindings section of README.md.

Build presets (from CMakePresets.json):
- `debug` / `release` — Unix/cross-platform (Ninja)
- `msvc-debug` / `msvc-release` — Windows MSVC
- `static-debug` / `static-release` — static library variants

Build options:
- `DEW_BUILD_TESTS` (ON) — build unit tests
- `DEW_BUILD_EXAMPLES` (ON) — build examples
- `BUILD_SHARED_LIBS` (ON) — shared vs static libraries

On the Mac the build dirs are `cmake-build-debug`, `cmake-build-release`, `cmake-build-wasm`; the
wasm build needs `source ~/dev/emsdk/emsdk_env.sh` first (system python is too old for emcc).

## Running Tests

Test framework is doctest. The MSVC test executable lives at `build/tests/Debug/private_interface_unit_tests.exe`.

**DLL dependency:** `laszip_api3d.dll` is not automatically copied to the test directory. If tests fail with exit code 127, copy it:
```bash
cp build/laszip_build_3.5.0_install/bin/laszip_api3d.dll build/tests/Debug/
```

**Running from MINGW64 bash:**
```bash
# Run all tests
/c/Users/jorge/dev/dewfall/build/tests/Debug/private_interface_unit_tests.exe

# Run tests matching a tag
/c/Users/jorge/dev/dewfall/build/tests/Debug/private_interface_unit_tests.exe "[converter]"
```

Exit code 127 from bash means a DLL is missing — check with `ldd <exe> | grep "not found"`.

## Project Structure

```
src/
  converter/           Converter pipeline (reader → sorter → tree → LOD → storage)
    dew/converter/       Public C headers (converter.h, converter_data_source.h, ...)
  render/              Graphics-agnostic renderer (callbacks, data sources, frustum)
    dew/render/          Public C headers (renderer.h, camera.h, draw_group.h, ...)
  common/              Shared types (error, format, containers)
    dew/common/          Public C headers (error.h, format.h)
  wasm/                Emscripten modules (renderer, data reader, decode worker)
examples/
  renderer/            OpenGL renderer example (gl_renderer.cpp, renderer_example.cpp)
    web/                 React + WebGL2 front end (Vite; dewfall.limilind.com)
tools/
  dew/                 The dew CLI: main.cpp dispatch + cmd_{convert,info,extract,copy,laz}.cpp
tests/
  private/             Internal unit tests (doctest)
  public/              Public API tests
backend/               prism static-file server for dewfall.limilind.com
deploy/                Dockerfile for the web deployment
CMake/                 Build modules and dependency definitions (3rdPartyPackages.cmake)
```

## Architecture: Converter Pipeline

```
Input Files → Reader (byte-targeted chunks, default 64 MiB / ≤8M pts)
  → Sorter (XYZ → Morton, sort, reorder buffers)
  → Tree Builder (octree subdivision down to node_point_limit leaves)
  → Leaf Collapse (at subtree finality: merge leaf subsets into per-node units, free the chunks)
  → LOD Generator (bottom-up sampling)
  → Storage Handler (preprocess + compress + write to disk)
```

### Key Components

| File | Role |
|------|------|
| `processor.hpp/cpp` | Central orchestrator. Event-driven threading with event pipes, thread pool, and 1GB read/sort memory budget for backpressure. |
| `reader.hpp/cpp` | `get_data_worker_t` calls user's `convert_data` callback in chunks sized to `read_chunk_byte_target` (default 64 MiB, clamped to [node_point_limit, 8M] points). `sort_worker_t` takes unsorted output and feeds it to the sorter. |
| `sorter.hpp/cpp` | Transforms XYZ coordinates to Morton192 codes (Z-order curve), sorts index array by morton value, reorders all attribute buffers to match. Determines LOD span from morton min/max. |
| `morton.hpp` | Morton code types (32/64/128/192-bit), encode/decode, LOD calculation, bitwise ops. |
| `tree.hpp/cpp` | Sparse octree. Each `tree_t` has 5 LOD levels with child bitmasks, skip arrays, and node IDs. Points routed to children by morton code. `tree_registry_t` manages all trees. Serialize/deserialize for persistence. |
| `tree_handler.hpp/cpp` | Manages tree lifecycle. `tree_initialize()` creates root, `tree_add_points()` subdivides recursively. |
| `tree_collapse.hpp/cpp` | Leaf collapse at finality: when a subtree drops below the done-morton watermark, each leaf's chunk subsets are k-way-merged into the leaf's own storage unit (collapsed ids, high `sub` bit 30) and the ingest chunks are freed via the checkpoint machinery once no tree references them (`tree_registry_t::chunk_tree_refs`). Runs at the start of each LOD pass; finality is gated on `tree_t::leaves_collapsed`. |
| `tree_lod_generator.hpp/cpp` | Bottom-up LOD generation. `lod_worker_t` reads child nodes, samples/aggregates, writes lower-resolution buffers. Batched for parallel execution. |
| `storage_handler.hpp/cpp` | File I/O with compression. `blob_manager_t` tracks free space (100MB pages). `lru_cache_t` for read caching. Async read with condition variable. |
| `memory_budget.hpp` | Renderer total-CPU-memory budget: `derive_budgets` (one knob → read cache / decoded backlog cap / resident budget / io clamp), heap-pressure brake levels, pre-load byte estimators. |
| `compression_preprocess.hpp/cpp` | Pre-compression transforms: delta encoding (morton/element/component/xor), constant band detection, offset subtraction (f64), decorrelation (u16x3 RGB → G,R-G,B-G), sort with permutation. |
| `compressor_zstd.hpp/cpp` | Zstandard compression backend (configurable level, default 9). |
| `compressor_fse.hpp/cpp` | Finite State Entropy (Huffman) compression. |
| `compressor_ans.hpp/cpp` | ANS (Asymmetric Numeral System) compression. |
| `byte_shuffle.hpp/cpp` | Byte transposition for better compression ratios. |
| `attributes_configs.hpp/cpp` | Attribute schema registry. Maps `attributes_id_t` to attribute sets and LOD downsampling rules. |
| `conversion_types.hpp` | Core types: `input_data_id_t` (data+sub composite ID), `storage_location_t` (file_id/size/offset), `storage_header_t` (point count, morton bounds, lod_span), `points_t` (header + attribute buffers), `tree_config_t` (scale/offset). |
| `perf_stats.hpp` | Per-phase I/O counters (bytes, time, mbps) for source read/write, sort, LOD read/write. Cache hit/miss tracking. |

### Public C API (converter)

Header: `src/converter/dew/converter/converter.h`

```c
// Create converter with cache file path
dew_converter_t *dew_converter_create(cache_filename, len, open_file_semantics, &error);

// Configure
dew_converter_set_compression(converter, method);         // none, zstd
dew_converter_set_compression_level(converter, level);
dew_converter_set_store_original_order(converter, flag);

// Set callbacks for file conversion and progress/error/done notifications
dew_converter_set_file_converter_callbacks(converter, callbacks);
dew_converter_set_runtime_callbacks(converter, callbacks);

// Add input and process
dew_converter_add_data_file(converter, buffers, buffer_count);
dew_converter_wait_idle(converter, &error);

// Query results
dew_converter_status(converter);          // idle, converting, done, error
dew_converter_get_compression_stats(converter, &stats);
dew_converter_get_perf_stats(converter, &stats);
```

File conversion callbacks (`dew_converter_file_convert_callbacks_t`):
- `pre_init` — estimate file size/point count
- `init` — open file, return header with offset/scale/min/max and attribute definitions
- `convert_data` — stream point chunks into provided buffers
- `destroy_user_ptr` — cleanup

## Architecture: Rendering System

Graphics-API-agnostic, callback-driven design. The renderer knows nothing about OpenGL/Vulkan — it produces draw groups that the consumer dispatches.

```
Consumer calls dew_renderer_frame(camera)
  → renderer iterates registered data sources
    → each data source's add_to_frame() produces draw groups
      → draw groups returned to consumer as array
        → consumer dispatches each group by draw_type to its own handlers
```

### Key Components

| File | Role |
|------|------|
| `renderer.h` | Public C API: create renderer, add/remove cameras, add/remove data sources, call `dew_renderer_frame()` to get draw groups for a frame. |
| `renderer_callbacks.hpp` | `callback_manager_t` — thread-safe dispatch of buffer/texture lifecycle callbacks (create, initialize, modify, destroy). Consumer implements these for their graphics API. |
| `data_source.h` | Data source interface: function pointer `dew_add_to_frame_t(camera, to_render, user_data)`. Each source decides what to draw based on camera. |
| `draw_group.h` | `dew_draw_group_t`: draw_type enum, array of `dew_draw_buffer_t` (buffer_mapping semantic + user_ptr handle), draw_size, lod_level. |
| `camera.h` | Camera control: view/projection matrices, arcball and FPS controllers, `dew_camera_look_at_aabb()`. |
| `frustum.hpp` | 6-plane frustum from view-projection matrix. AABB intersection test (outside/intersects/inside). |
| `buffer.hpp` | Buffer wrapper with rendered flag and async release callback. |

### Draw Types

`dew_draw_type_t` enum: `dew_flat_points`, `dew_dyn_points_1`, `dew_dyn_points_3`, `dew_dyn_points_crossfade`, `dew_aabb_triangle_mesh`, `dew_skybox_triangle`, `dew_axis_gizmo_lines`, `dew_origin_anchor_mesh`, `dew_environment_bg`, `dew_node_bbox_lines`.

Each draw type has its own `buffer_mapping` enum defining the semantic meaning of each buffer slot (position, color, camera matrix, index, etc.).

### Built-in Data Sources

| File | What it renders |
|------|-----------------|
| `data_source_flat_points` | Entire LAS/LAZ file as flat point cloud |
| `data_source_aabb` | Bounding box wireframes |
| `data_source_skybox` | Skybox cube |
| `data_source_axis_gizmo` | 3D axis indicator |
| `data_source_origin_anchor` | World origin marker |
| `data_source_environment` | Environment background |

### Converter-integrated rendering

Files in `src/converter/` that bridge converter and renderer:
- `data_source_converter.hpp/cpp` — data source wrapping the converter's tree. Drives the per-frame render pipeline, with pixel-error LOD selection, GPU memory budgeting, a total CPU-memory budget (`dew_converter_data_source_set_memory_budget`) and a wasm heap-pressure brake.
- `frustum_tree_walker.hpp/cpp` — walks the octree with frustum culling
- `render_node.hpp` — `render_node_t`: one entry per visible node, carrying its IO/GPU/fade state machines (`render_node_io_state`, `render_node_gpu_state`, `render_node_fade_state`), GPU buffers, and cached camera distance. Also defines `frame_timings_t`.
- `render_pipeline.hpp/cpp` — the unified render pipeline over a `render_list_t` (a distance/LOD-sorted vector of `render_node_t`). Each frame: `build_render_list` (diff the walker output against the previous frame's list, spawning fade-ins for new nodes and fade-outs for departed ones), `process_io_and_upload` (single pass: compute distances, advance IO state, schedule closest-first loads under byte gates, decompress/morton-decode on a `convert_pool` worker thread, then upload to the GPU under budget), `update_fades` (frame-time-based crossfade), and `emit_draws` (steady opaque pass + fading pass).
- `native_node_data_loader.hpp/cpp` — async node data loading from disk
- `worker_node_data_loader.hpp/cpp` — wasm decode-worker loading path (globalThis.__dewDecodePool)

### OpenGL Example

`examples/renderer/` contains a full OpenGL implementation:
- `gl_renderer.h/cpp` — implements all renderer callbacks (create/init/modify/destroy buffer), dispatches draw groups to per-type handler classes (gl_aabb_handler, gl_flat_points_handler, gl_dyn_points_handler, etc.)
- `renderer_example.cpp` — SDL window setup, main loop calling `dew_renderer_frame()` and `gl_renderer::draw()`

Pattern for implementing a custom renderer:
1. Create `dew_renderer_callbacks_t` with your graphics API's buffer/texture lifecycle functions
2. Call `dew_renderer_set_callback(renderer, callbacks, user_ptr)`
3. Each frame: call `dew_renderer_frame(renderer, camera)` → get array of `dew_draw_group_t`
4. For each draw group: dispatch by `draw_type`, iterate `buffers`, bind by `buffer_mapping` semantic, issue draw call with `draw_size`

## The .dew File Format

**Compression header** (16 bytes, magic `PCM\1`):
```
magic[4]  method  type_size  component_count  flags  uncompressed_size[4]  compressed_size[4]
```

Compression methods: none (0), zstd (2), huff0 (3), constant (4), ans (5).

**DEW2 bucket layout** (destination mode, `bucket_format.hpp`): a converted dataset in an object
store is `manifest` (256B root manifest, the only mutable object), `bands/{band:08}` (immutable
band manifests) and `data/{id:08x}` — exactly ONE compressed blob per object (a node's attribute
buffer, a serialized tree, or the terminal registry/attrs). Object size == blob size; every
dataset read is a whole-object GET (no `Range:` header — `vio read_object_all`). Only the
cache tier's temporary `spill/` segments use ranged GETs. `store_original_order` survives
collapse as reordered values but loses chunk attribution.

**Magic compatibility (pre-rename datasets):** new writes use `DEW2` (bucket root manifest) and
`DEW\0` (128B superblock); readers ALSO accept the pre-rename `JLP2` / `JLP\0` byte values, so
datasets and caches from before the rename keep opening. The local superblock self-migrates on
the first checkpoint; `dew copy` migrates a bucket dataset.

Preprocessing flags (bitfield):
- `0x01` delta encoded, `0x02` constant bands, `0x04` offset subtracted, `0x08` sort permutation
- `0x10` decorrelated, `0x20` component delta, `0x40` element delta, `0x80` xor delta

Point data types (`dew_type_t`): u8, i8, u16, i16, u32, i32, m32, r32, u64, i64, m64, r64, m128, m192.
Components: 1, 2, 3, 4, 4x4.

## Dependencies

Managed via cmake-dep (CMake/3rdPartyPackages.cmake):

| Library | Version | Purpose |
|---------|---------|---------|
| zstd | 1.5.7 | Zstandard compression |
| laszip | 3.5.0 | LAS/LAZ point cloud format |
| libmorton | 0.2.10 | Morton/Z-order curve encoding |
| SDL | 3.1.6 | Window/input for examples |
| glm | 1.0.1 | Math library |
| imgui | 1.91.6 | UI for examples |
| fmt | 10.1.1 | String formatting |
| doctest | 2.4.12 | Unit testing |
| argh | 431bf32 | Argument parsing |
| unordered_dense | 4.1.2 | Fast hash map/set (ankerl) |
| vio | 86bbea0 | Async I/O / event loop / object store |
| stbimage | b42009b | Image loading |
| cmakerc | 952ff | CMake resource compiler |
| nanobind | 2.13.0 | Python bindings (only when `DEW_BUILD_PYTHON=ON`) |
| robin_map | 1.4.1 | nanobind's hash map (its release tarballs omit the submodule) |

Pulled in transitively by vio, not listed in `3rdPartyPackages.cmake`: libuv
(async I/O), LibreSSL (TLS), ada (URL parsing). There is no curl dependency —
HTTP goes through vio.

## Debugging .dew Files with dew extract

`dew extract` is the primary tool for inspecting datasets without a renderer.

```bash
DEW=path/to/file.dew
EXE="cmake-build-debug/tools/dew/dew extract"
```

For LAS/LAZ inputs, `dew laz <file>` prints the header (add `--vlrs` for records,
`--points --offset N -n M` for a point subrange, `--csv` for machine-readable output).

### List available attributes
```bash
$EXE $DEW
```

### Tree introspection

```bash
# High-level overview: root tree ID, scale, offset, tree count
$EXE $DEW --summary

# List all trees with per-level node counts and sub-tree refs
$EXE $DEW --trees

# Full dump of tree 0: every node, subsets, storage map
$EXE $DEW --tree 0

# Single node detail (tree 0, level 1, index 2) with storage locations
$EXE $DEW --node 0:1:2
```

Key things to look for when debugging:
- **Zero point counts on interior nodes** — LOD nodes (marked `[lod]`) should have non-zero counts. Leaf subsets have `input_id=N.M` where the high bit of `sub` is clear; LOD subsets have the high bit set (shows as `input_id=N.2147483648`).
- **Sub-tree connectivity** — `--trees` shows which child trees each tree references. The root tree (shown in `--summary`) should be the entry point; all other trees should be reachable through sub-tree refs.
- **Storage map entries** — `--tree N` dumps the full storage map. Each `input_data_id` maps to an `attrib_id` and a list of storage locations (offset/size/file_id) for each attribute buffer.

### Attribute buffer extraction

```bash
# Hex dump of first buffer for attribute "xyz"
$EXE $DEW xyz

# Select a specific buffer index, print 10 elements starting at offset 100
$EXE $DEW xyz --index 5 --offset 100 -n 10

# Extract buffers 0-3 as binary to a file
$EXE $DEW intensity --range 0-3 -o out.bin

# Extract 10 random buffers
$EXE $DEW rgb --random 10 --seed 42
```
