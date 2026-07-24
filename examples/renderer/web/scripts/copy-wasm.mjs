// Copies the built Emscripten renderer module (points_render.mjs + .wasm) into public/ so Vite serves it
// at the app root. Runs automatically before `dev` and `build` (see package.json). Point it at a specific
// build directory with POINTS_WASM_DIR=/path/to/dir.
import { access, copyFile, mkdir } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(here, '..'); // examples/renderer/web
const repoRoot = resolve(webRoot, '../../..'); // points/

const FILES = ['points_render.mjs', 'points_render.wasm'];

const candidates = [
  process.env.POINTS_WASM_DIR,
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
    '[copy-wasm] Could not find points_render.mjs.\n' +
      '  Build it first:  cmake --build <wasm-build-dir> --target points_render_wasm\n' +
      '  Or set POINTS_WASM_DIR to the directory containing points_render.mjs/.wasm.\n' +
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
