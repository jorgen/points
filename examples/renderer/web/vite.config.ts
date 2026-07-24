import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// The Emscripten module (points_render.mjs) and its .wasm live in public/ and are loaded at runtime via a
// dynamic import (see src/pointsRender.ts), so Vite serves them verbatim and never tries to bundle the
// Emscripten glue or the WebAssembly binary. Single-threaded build => no COOP/COEP headers needed.
export default defineConfig({
  plugins: [react()],
  server: { port: 5173 },
  preview: { port: 4173 },
});
