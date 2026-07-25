# points.limilind.com

A limilind subservice (same shape as `cube`/`jorcs`, `lists`, `lineup`): a React + Vite + TypeScript SPA
that loads the WebGL2 point-cloud renderer compiled to **WebAssembly** and streams the point cloud straight
from **S3 in the browser** (via the WASM's own fetch client). There is **no API and no database** — the
backend is a tiny `prism` static-file server.

## Layout

- `examples/renderer/web/` — the SPA (the existing renderer example is the frontend).
- `backend/` — `points_server`, a `prism` static-file server (`app.static_files("/", dist, spa_fallback=true)`
  + `/api/health`). Self-contained CMake project consuming vio/structify/prism.
- `deploy/Dockerfile` — multi-stage image (emsdk builds the renderer WASM → node builds the SPA → the prism
  server is built against pinned vio/structify/prism → slim runtime serving `/app/dist` on `:8080`).
- `.github/workflows/deploy.yml` — on push to `master` (paths that affect the bundle) build+push
  `ghcr.io/jorgen/points-web` then SSH `/opt/stack/update.sh points <sha>`.

## Local run

```bash
# 1. Build the renderer WASM (needs EMSDK):
cmake --preset emscripten
cmake --build cmake-build-wasm --target points_render_wasm   # NB: not the preset default (points_data_wasm)

# 2. Dev SPA (Vite proxies nothing; the WASM talks to S3 directly):
cd examples/renderer/web && npm ci && npm run dev            # copy-wasm runs automatically

# Or the production path: build the SPA, then serve it with the prism backend:
cd examples/renderer/web && npm run build                    # -> examples/renderer/web/dist
cmake -S backend -B backend/build -G Ninja && cmake --build backend/build --target points_server
./backend/build/points_server examples/renderer/web/dist 8080
```

## Deploy (two repos)

`points` (this repo) only builds+pushes the image and triggers a redeploy. All routing/TLS/DNS live in
**`github.com/jorgen/limilind-edge`** ("no visible infrastructure"). To bring `points.limilind.com` up:

1. In **this** repo, add GitHub secrets `DEPLOY_HOST`, `DEPLOY_SSH_KEY`, `DEPLOY_KNOWN_HOSTS` (same values
   as the other limilind apps).
2. In **`limilind-edge`**: add a `points` compose service (`image ghcr.io/jorgen/points-web`,
   `networks: [web]`, `POINTS_HOST=0.0.0.0`/`POINTS_PORT=8080`, no Postgres), append
   `;points.limilind.com=points:8080` to `GATEWAY_ROUTES` (+ `.env`), add a `points` welcome card, and add a
   GoDaddy A-record `points.limilind.com` → droplet IP. The gateway auto-issues the TLS cert by SNI.

## Public / keyless default (prerequisites)

The SPA defaults to the public dataset `s3://limilind-public/points/synthetic` (`region=eu-north-1`) and
auto-connects, so the page renders with no input. For that anonymous read to actually succeed:

- **vio anonymous S3**: the WASM must issue an *unsigned* GET when no credentials are given (public objects).
  Today vio always SigV4-signs; a small addition to `s3_object_store.h` (skip the `Authorization`/`x-amz-*`
  headers when the access key is empty) is required, then re-pin vio for the WASM build.
- **Bucket policy**: `s3://limilind-public` needs a public-read policy on `points/*`.
- **CORS**: `s3://limilind-public` must allow the `points.limilind.com` origin for `GET` + **ranged** reads
  (expose `Content-Length`/`Content-Range`; allow `Range`, `Authorization`, `x-amz-*` request headers).
