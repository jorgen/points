<p align="center">
  <img src="assets/banner.svg" alt="dewfall — point clouds, settled." width="720">
</p>

# dewfall

**dewfall** is a point cloud management and rendering library. It converts arbitrary point cloud
input into a morton-ordered octree with level-of-detail, stored in a compressed, random-accessible
format (`.dew` files) that streams equally well from a local disk or an object store — and it ships
a graphics-API-agnostic renderer that draws those clouds at interactive rates on the desktop and in
the browser.

Named for the moment a cloud settles into points. Friends call it **dew**.

- **Live demo:** [dewfall.limilind.com](https://dewfall.limilind.com) — the WebGL2/WASM renderer
  streaming a dataset straight from S3.
- C++23 core, no RTTI, no exceptions. The public API is pure C (C99/C11 compatible), prefixed `dew_`.
- AGPL-3.0.

## Highlights

- **Converter pipeline** — streams arbitrary input (LAS/LAZ built in, or your own reader callbacks)
  through morton sorting, octree subdivision, leaf collapse, and bottom-up LOD generation, with a
  bounded memory budget and crash-safe checkpointing throughout.
- **The `.dew` format** — every node's attribute buffer is its own compressed blob. In an object
  store a dataset is one immutable object per blob, so every read is a whole-object GET: no range
  requests, CDN- and HTTP-cache-friendly, resumable uploads. Preprocessing (delta, decorrelation,
  byte shuffle) + zstd keep blobs small; a 200K-point node's xyz is typically well under 1 MB.
- **Cloud-native conversion** — convert with a destination bucket (`s3://`, `az://`) and finished
  subtrees upload incrementally while the conversion runs; the local cache file spills and evicts
  under a configurable cap.
- **Streaming renderer** — frustum-walked octree with pixel-error LOD selection, per-point LOD in
  the shader, crossfaded transitions, GPU and CPU memory budgets with a heap-pressure brake for
  mobile browsers, and render-time virtual subdivision of dense leaves.
- **Bring your own graphics API** — the renderer emits draw groups through callbacks; the repo
  includes a full OpenGL example and a WebGL2/Emscripten build with a React front end.

## Building

```bash
cmake --preset release
cmake --build cmake-build-release
```

| Target | What it is |
|---|---|
| `dew_converter` | converter library |
| `dew_render` | rendering library |
| `dew_common` | shared types |
| `dew` | the dewfall CLI — convert, inspect, copy, LAS/LAZ introspection (see below) |
| `renderer_example` | SDL/OpenGL viewer |
| `dew_render_wasm` / `dew_decode_worker` | browser renderer + decode-worker modules (Emscripten) |

Tests are doctest (`private_interface_unit_tests`, `public_interface_unit_tests`), on by default via
`DEW_BUILD_TESTS`.

## The `dew` CLI

One binary, five subcommands — this is the full-fledged converter and dataset toolbox:

```bash
dew convert input.laz -o output.dew        # convert (multiple inputs / wildcards work too)
dew convert *.laz -o s3://bucket/set \
    -C 'region=eu-north-1'                 # convert straight into a bucket: finished subtrees
                                           # upload incrementally while the conversion runs
dew info output.dew                        # compression / performance / cache statistics
dew info s3://bucket/set \
    -C 'region=eu-north-1;anonymous=true'  # ...works on cloud datasets too
dew extract output.dew --summary           # octree overview (--trees, --tree N, --node N:L:I)
dew extract output.dew xyz -n 10           # peek at attribute data (or extract it with -o)
dew copy output.dew s3://bucket/set        # copy/migrate a dataset (local <-> bucket)
dew laz input.laz                          # LAS/LAZ introspection: header + VLRs (--vlrs)
dew laz input.laz --points --offset 100000 -n 5   # ...or print/extract a point subrange (--csv)
```

`dew convert` knobs: `-c/--compression` (zstd default), `-n/--node-points` (blob-size lever),
`--cache`/`--cache-max-bytes` (local cache for a cloud destination). Run `dew help <command>`
for everything else.

## Driving the converter from code

The CLI is a thin client of the pure-C API — embed the same pipeline in your own tooling:

```c
struct dew_error_t *error = NULL;
struct dew_converter_t *converter = dew_converter_create(
    "output.dew", strlen("output.dew"), dew_open_file_semantics_truncate, &error);
struct dew_converter_str_buffer file = {"input.laz", strlen("input.laz")};
dew_converter_add_data_file(converter, &file, 1);
dew_converter_wait_idle(converter, &error);
dew_converter_destroy(converter);
```

LAS/LAZ input is built in; arbitrary sources plug in through
`dew_converter_set_file_converter_callbacks` (pre_init / init / convert_data callbacks that
stream your format's points into the provided buffers). Converting straight into a bucket is
one call — finished subtrees upload while the conversion runs:

```c
struct dew_converter_t *converter = dew_converter_create_with_destination(
    "cache.dew", strlen("cache.dew"),
    "s3://my-bucket/my-dataset", strlen("s3://my-bucket/my-dataset"),
    "region=eu-north-1", strlen("region=eu-north-1"),
    dew_open_file_semantics_truncate, &error);
```

Render it — the renderer knows nothing about your graphics API; you implement buffer callbacks and
dispatch the draw groups it emits (see `examples/renderer/gl_renderer.cpp` for a complete OpenGL
implementation):

```c
struct dew_renderer_t *renderer = dew_renderer_create();
dew_renderer_set_callback(renderer, my_gl_callbacks, user_ptr);
struct dew_converter_data_source_t *ds = dew_converter_data_source_create(url, strlen(url), &error, renderer);
dew_renderer_add_data_source(renderer, dew_converter_data_source_get(ds));
// each frame:
struct dew_frame_t frame = dew_renderer_frame(renderer, camera);
// dispatch frame's draw groups by draw_type
```

## Architecture

```
Input files → Reader (byte-targeted chunks, 64 MiB default)
  → Sorter (xyz → morton codes, sort, reorder attributes)
  → Tree builder (octree subdivision to the node point limit)
  → Leaf collapse (per-node storage units at subtree finality)
  → LOD generator (bottom-up representative sampling)
  → Storage (preprocess + compress + write; optional incremental upload)
```

The renderer walks the same octree with the camera frustum, streams the nodes the screen needs
closest-first, decodes them off the main thread, and uploads under byte budgets — see
`src/converter/data_source_converter.*` and `src/converter/render_pipeline.*`.

Datasets written before the rename (`JLP` magics) still open: readers accept both the old and new
magic bytes, and `dew copy` rewrites a dataset to the current format.

## Roadmap

- **Python bindings** — `import dew`: dataset access and conversion from Python.
- **ML training support** — efficient point/attribute sampling suitable for feeding point cloud
  model training pipelines directly from `.dew` datasets.

## License

AGPL-3.0 (GNU Affero General Public License v3) — see [LICENSE](LICENSE).
