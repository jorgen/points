# points — WebGL2 / WebAssembly renderer (React example)

A minimal React app that streams and draws a point-cloud dataset from S3 in the browser, using the
WebAssembly renderer built from `examples/renderer/renderer_wasm.cpp`.

**Architecture.** React owns the `<canvas>`, the DOM input, and the animation loop; the WASM module owns
WebGL2 rendering, the octree stream/decode pipeline, and the arcball camera math. Rendering is
**dirty-driven**: the WASM calls a JS `requestUpdate` callback whenever the frame changes (input, a
network read completing, decode, GPU upload); React coalesces those into a single `requestAnimationFrame`
that calls `frame()` and then goes idle — nothing renders continuously.

## Prerequisites

1. Build the renderer module (produces `points_render.mjs` + `points_render.wasm`):

   ```bash
   cmake --preset emscripten
   cmake --build cmake-build-wasm --target points_render_wasm
   ```

   `scripts/copy-wasm.mjs` finds the artifacts in `cmake-build-wasm/src/wasm/` automatically; set
   `POINTS_WASM_DIR` if your build directory differs.

2. Node 18+ and a point-cloud dataset in an S3-compatible store (see the converter to produce one).

## Run

```bash
npm install
npm run dev      # http://localhost:5173  (copies the wasm artifacts in first)
```

Fill in the endpoint / bucket / prefix / credentials and click **Connect**. For a local minio dataset:

- Endpoint `http://127.0.0.1:9000`, path-style checked
- Bucket `pointstest`, prefix `synth`
- Access key / secret `minioadmin` / `minioadmin`

You can also prefill (and auto-connect) via query params — handy for shareable links, **local testing
only** since it puts credentials in the URL:

```
http://localhost:5173/?endpoint=http://127.0.0.1:9000&bucket=pointstest&prefix=synth&pathStyle=1&accessKeyId=minioadmin&secretAccessKey=minioadmin&autoconnect=1
```

## Controls

| Input | Action |
| --- | --- |
| drag | orbit |
| right-drag | pan |
| shift + right-drag | dolly |
| scroll | zoom |
| Attribute dropdown | switch the colored attribute |

## CORS

The dataset store must allow cross-origin reads from the app origin: allow methods `GET`, request headers
`authorization, x-amz-date, x-amz-content-sha256, x-amz-security-token, range`, and (for ranged reads)
expose `content-length, content-range`. minio permits this by default.

## Build

```bash
npm run build    # -> dist/ (static; deployable to any host, no COOP/COEP needed)
npm run preview
```
