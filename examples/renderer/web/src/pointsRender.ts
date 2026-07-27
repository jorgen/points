// Loads the Emscripten renderer module (points_render.mjs, built from examples/renderer/renderer_wasm.cpp)
// and exposes typed wrappers for its embind API. The .mjs + .wasm are served from the app root (copied
// into public/ by scripts/copy-wasm.mjs); the module is imported at runtime so Vite never bundles the
// Emscripten glue or the WebAssembly binary.

// A connection has two parts: the dataset `url` (scheme + bucket/prefix, e.g. s3://bucket/prefix) and a
// `connectionString` -- the remaining connection parameters as a ";"-separated key=value list, using the
// EXACT same grammar and keys as the CLI tools (converter --connection, jlp_copy -s/-d), e.g.
//   endpoint=https://host:9000;access_key_id=..;secret_access_key=..;region=us-east-1;path_style=true
export interface Connection {
  /** Dataset location: scheme + bucket/prefix, e.g. "s3://bucket/prefix". */
  url: string;
  /** All other connection parameters as the ";"-separated CLI connection-string grammar. */
  connectionString: string;
}

export interface Aabb {
  min: [number, number, number];
  max: [number, number, number];
}

/** One WebGL2 renderer bound to a canvas + dataset. Mirrors the embind `Renderer` class. */
export interface Renderer {
  /** Pump the streaming loops and draw once, at the given drawing-buffer pixel size. */
  frame(width: number, height: number): void;
  /** Orbit. Deltas are normalized (pixel movement / canvas CSS size). */
  cameraRotate(ndx: number, ndy: number): void;
  cameraRoll(nd: number): void;
  cameraPan(ndx: number, ndy: number): void;
  /** Pan within the dataset's ground plane (the desktop app's ctrl+right-drag). */
  cameraPanGround(ndx: number, ndy: number): void;
  cameraDolly(nd: number): void;
  cameraZoom(nz: number): void;
  /** Restore the initial fitted view. */
  resetView(): void;
  setAttribute(name: string): void;
  getAttributeNames(): string[];
  getAabb(): Aabb;
  getPointsRendered(): number;
  /** Point splat world size. */
  setPointSize(v: number): void;
  /** Level-of-detail scale base (higher = coarser LOD kept). */
  setLodScaleBase(v: number): void;
  /** Octree refinement budget: smaller screen-space pixel error = more detail (+ more streaming). */
  setPixelErrorThreshold(v: number): void;
  /** Runtime per-node LOD: target on-screen spacing (px) between drawn points → uniform density. */
  setRenderDensityPx(v: number): void;
  /** GPU memory budget in MB; the streamer evicts to stay under it. */
  setGpuMemoryBudgetMb(mb: number): void;
  /** Toggle the per-node bounding-box overlay. */
  setShowBoundingBoxes(show: boolean): void;
  /** Register the JS redraw callback; the renderer invokes it whenever the frame becomes dirty. */
  setRequestUpdate(cb: () => void): void;
  /** Release GL + streaming resources. Idempotent; the instance must not be used afterwards. */
  dispose(): void;
}

interface PointsRenderModule {
  createRenderer(canvasSelector: string, url: string, connectionString: string): Promise<Renderer | null>;
}

type ModuleFactory = (moduleArg?: Record<string, unknown>) => Promise<PointsRenderModule>;

let modulePromise: Promise<PointsRenderModule> | null = null;

/** Load (once) and instantiate the Emscripten renderer module. */
export function loadPointsRender(): Promise<PointsRenderModule> {
  if (!modulePromise) {
    modulePromise = instantiate().catch((err: unknown) => {
      modulePromise = null; // allow a retry after a transient failure
      throw err;
    });
  }
  return modulePromise;
}

async function instantiate(): Promise<PointsRenderModule> {
  // Resolve the artifacts against the app base so this works at the site root or under a sub-path.
  const base = document.baseURI;
  const glueUrl = new URL('points_render.mjs', base).href;
  // Import the Emscripten glue through a Blob URL rather than a normal module specifier: points_render.mjs
  // is a /public asset, and Vite's dev server refuses to serve a public file through its module-transform
  // pipeline ("...should not be imported from source code"). A Blob import runs entirely in the browser,
  // bypassing Vite in both dev and build. locateFile then points the module at its .wasm, which is fetched
  // as a plain static asset (that Vite does serve).
  const source = await fetch(glueUrl).then((r) => {
    if (!r.ok) throw new Error(`failed to fetch ${glueUrl}: HTTP ${r.status}`);
    return r.text();
  });
  const blobUrl = URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
  try {
    const factory = (await import(/* @vite-ignore */ blobUrl)).default as ModuleFactory;
    return await factory({ locateFile: (path: string) => new URL(path, base).href });
  } finally {
    URL.revokeObjectURL(blobUrl);
  }
}
