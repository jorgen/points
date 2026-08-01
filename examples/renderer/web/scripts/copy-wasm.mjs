// Copies the built Emscripten renderer module (dew_render.mjs + .wasm) into public/ so Vite serves it
// at the app root. Runs automatically before `dev` and `build` (see package.json). Point it at a specific
// build directory with DEW_WASM_DIR=/path/to/dir.
import { access, copyFile, mkdir } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(here, '..'); // examples/renderer/web
const repoRoot = resolve(webRoot, '../../..'); // dewfall/

const FILES = ['dew_render.mjs', 'dew_render.wasm'];

// Optional pure-CPU decode worker module. When present it enables off-main-thread decode (a pool of these
// runs behind the worker_node_data_loader); when absent the app decodes inline on the main thread. Copied
// best-effort so a render-only build still works.
const OPTIONAL_FILES = ['dew_decode_worker.mjs', 'dew_decode_worker.wasm'];

const candidates = [
  process.env.DEW_WASM_DIR,
  resolve(repoRoot, 'cmake-build-wasm/src/wasm'),
  resolve(repoRoot, 'build/cmake-build-wasm/src/wasm'),
  resolve(repoRoot, 'build/linux-wasm/src/wasm'),
].filter((c) => typeof c === 'string');

async function findSourceDir() {
  for (const dir of candidates) {
    try {
      await access(join(dir, FILES[0]));
      return dir;
    } catch {
      // keep looking
    }
  }
  return null;
}

const src = await findSourceDir();
if (!src) {
  console.error(
    '[copy-wasm] Could not find dew_render.mjs.\n' +
      '  Build it first:  cmake --build <wasm-build-dir> --target dew_render_wasm\n' +
      '  Or set DEW_WASM_DIR to the directory containing dew_render.mjs/.wasm.\n' +
      'Searched:\n' +
      candidates.map((c) => '  ' + c).join('\n'),
  );
  process.exit(1);
}

const publicDir = join(webRoot, 'public');
await mkdir(publicDir, { recursive: true });
for (const f of FILES) {
  await copyFile(join(src, f), join(publicDir, f));
  console.log(`[copy-wasm] ${f}  <-  ${src}`);
}

for (const f of OPTIONAL_FILES) {
  try {
    await access(join(src, f));
  } catch {
    console.log(`[copy-wasm] (optional) ${f} not built — inline decode will be used`);
    continue;
  }
  await copyFile(join(src, f), join(publicDir, f));
  console.log(`[copy-wasm] ${f}  <-  ${src}`);
}
