// Decode Web Worker: runs the pure-CPU decode (decompress -> deserialize -> morton/LOD decode) off the
// browser main thread, so streaming a point cloud never janks rendering. The render module posts COMPRESSED
// blob bytes here (via decodeWorkerPool.ts) and gets GPU-ready vertex/attribute/rep_level buffers back as
// Transferable ArrayBuffers (zero-copy).
//
// Pairs with src/wasm/decode_worker.cpp (the wasm module built as `DewDecodeWorker`). Classic
// "wasm-in-a-worker" glue: load the module, decode on message, transfer the results back. A pool of these is
// managed by decodeWorkerPool.ts; the C++ worker_node_data_loader is the consumer.

type DecodeRequest = {
  id: number;
  treeScale: number;
  treeOffset: [number, number, number];
  formats: { type: number; components: number }[]; // length 4
  buffers: (Uint8Array | null)[];                   // length 4, COMPRESSED bytes
  wantSalvage?: boolean;                             // leaf: also return the raw points+attr blobs (virtual LOD)
};

// Load the decode module (dew_decode_worker.mjs) the same Vite-safe way the render module does: fetch the
// Emscripten glue as text and import it through a Blob URL, so the /public asset never goes through Vite's
// module-transform pipeline. locateFile then resolves the sibling .wasm. Resolved against the app origin
// root (where copy-wasm places the artifacts); a sub-path deployment would need the base passed in.
let modulePromise: Promise<any> | null = null;
function getModule(): Promise<any> {
  if (!modulePromise) modulePromise = instantiate();
  return modulePromise;
}

async function instantiate(): Promise<any> {
  const glueUrl = new URL('/dew_decode_worker.mjs', self.location.href).href;
  const source = await fetch(glueUrl).then((r) => {
    if (!r.ok) throw new Error(`failed to fetch ${glueUrl}: HTTP ${r.status}`);
    return r.text();
  });
  const blobUrl = URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
  try {
    const factory = (await import(/* @vite-ignore */ blobUrl)).default as (arg?: Record<string, unknown>) => Promise<any>;
    return await factory({ locateFile: (path: string) => new URL('/' + path, self.location.href).href });
  } finally {
    URL.revokeObjectURL(blobUrl);
  }
}

self.onmessage = async (ev: MessageEvent<DecodeRequest>) => {
  const req = ev.data;
  const Module = await getModule();

  // decodeNode returns an object whose vertex/attribute/repLevel are typed_memory_views into the wasm heap.
  // Copy each into its own ArrayBuffer immediately (before any further wasm allocation can move the heap),
  // then transfer those buffers back to the main thread.
  const r = Module.decodeNode({
    treeScale: req.treeScale,
    treeOffset: req.treeOffset,
    formats: req.formats,
    buffers: req.buffers, // the worker copies these into wasm memory
    wantSalvage: req.wantSalvage === true,
  });

  // decodeNode returns owned Uint8Arrays (or null for an absent buffer, e.g. repLevel on a non-morton node);
  // it has already freed its wasm-side node, so nothing to free here. Take each buffer and transfer it.
  // v.buffer is typed ArrayBufferLike (includes SharedArrayBuffer); ours is always a plain, transferable
  // ArrayBuffer (a fresh Uint8Array from the decode module), so narrow it.
  const take = (v: Uint8Array | null | undefined): ArrayBuffer | null => (v ? (v.buffer as ArrayBuffer) : null);
  const vertex = take(r.vertex);
  const attribute = take(r.attribute);
  const repLevel = take(r.repLevel);
  const salvagePoints = take(r.salvagePoints); // present only when wantSalvage (leaf); else undefined -> null
  const salvageAttr = take(r.salvageAttr);

  const reply = {
    id: req.id,
    pointCount: r.pointCount,
    hasLodOrder: r.hasLodOrder,
    drawType: r.drawType,
    vertexType: r.vertexType,
    vertexComponents: r.vertexComponents,
    attributeType: r.attributeType,
    attributeComponents: r.attributeComponents,
    offset: r.offset,
    prefixCount: r.prefixCount,
    vertex,
    attribute,
    repLevel,
    salvagePoints,
    salvageAttr,
  };
  // Transfer the (present) ArrayBuffers — zero-copy handoff to the main thread.
  const transfer = [vertex, attribute, repLevel, salvagePoints, salvageAttr].filter(
    (b): b is ArrayBuffer => b !== null,
  );
  (self as unknown as Worker).postMessage(reply, transfer);
};
