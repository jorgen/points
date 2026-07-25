# syntax=docker/dockerfile:1
#
# Multi-stage build for points (points.limilind.com): one image serving the built React SPA, which loads
# the WebGL2 point-cloud renderer compiled to WebAssembly. The app is entirely client-side — the point
# cloud is streamed straight from S3 by the WASM's own fetch client — so the server only serves static
# files. Build context is the repo root:
#   docker build -f deploy/Dockerfile -t points-web .

# ---------- WASM (C++ renderer -> WebAssembly via Emscripten) ----------
# Pin the emsdk version the renderer is developed against (keep in sync with deploy.yml).
FROM emscripten/emsdk:6.0.3 AS wasm
WORKDIR /src
COPY . ./
# Configure the points project under Emscripten and build only the WebGL2 renderer module. (The build-preset
# 'emscripten' defaults to points_data_wasm, so the target is named explicitly.) Fetches the pinned CmDep
# dependencies (vio, zstd, fmt, glm, ...) during configure.
RUN emcmake cmake -S . -B /wasm-build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /wasm-build --target points_render_wasm \
 && test -f /wasm-build/src/wasm/points_render.mjs \
 && test -f /wasm-build/src/wasm/points_render.wasm

# ---------- SPA (React + Vite) ----------
FROM node:20-slim AS web
WORKDIR /web
COPY examples/renderer/web/package.json examples/renderer/web/package-lock.json ./
RUN npm ci
COPY examples/renderer/web/ ./
# The renderer bundle from the wasm stage; copy-wasm.mjs (npm prebuild) pulls it from POINTS_WASM_DIR.
COPY --from=wasm /wasm-build/src/wasm/points_render.mjs /wasm-build/src/wasm/points_render.wasm /wasm-artifacts/
ENV POINTS_WASM_DIR=/wasm-artifacts
RUN npm run build   # copy-wasm -> tsc --noEmit && vite build -> /web/dist

# ---------- backend (prism static-file server) ----------
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive CC=gcc-14 CXX=g++-14
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++-14 ninja-build git ca-certificates python3-pip \
 && pip3 install --break-system-packages cmake \
 && rm -rf /var/lib/apt/lists/*

# Pinned dependency commits — keep in sync with .github/workflows/deploy.yml.
ARG VIO_REF=2051451f4067e580a09d036323834b69ce44661c
ARG STRUCTIFY_REF=7ff77ebeb8da5c28d24d88a55abde350e0f0e0e4
ARG PRISM_REF=95ce77f03a7c3c2ebea966bae39eeffa79a48302
RUN git clone --filter=blob:none https://github.com/jorgen/vio.git       /deps/vio       && git -C /deps/vio       checkout -q "$VIO_REF" \
 && git clone --filter=blob:none https://github.com/jorgen/structify.git /deps/structify && git -C /deps/structify checkout -q "$STRUCTIFY_REF" \
 && git clone --filter=blob:none https://github.com/jorgen/prism.git     /deps/prism     && git -C /deps/prism     checkout -q "$PRISM_REF"

COPY backend/ /src/backend/
RUN cmake -S /src/backend -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
      -DPOINTS_WEB_VIO_DIR=/deps/vio \
      -DPOINTS_WEB_STRUCTIFY_DIR=/deps/structify \
      -DPOINTS_WEB_PRISM_DIR=/deps/prism \
 && cmake --build /build --target points_server \
 && ldd /build/points_server || true

# ---------- runtime ----------
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates tzdata \
 && rm -rf /var/lib/apt/lists/*
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt \
    POINTS_DIST=/app/dist \
    POINTS_HOST=0.0.0.0 \
    POINTS_PORT=8080
COPY --from=build /build/points_server /usr/local/bin/points_server
COPY --from=web   /web/dist            /app/dist
RUN useradd --system --uid 10001 --user-group app
USER app
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/points_server"]
