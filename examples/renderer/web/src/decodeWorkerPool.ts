// The main-thread half of the off-main-thread decode. Installs `globalThis.__pointsDecodePool`, which the
// wasm render module (worker_node_data_loader.cpp) calls via embind:
//
//   __pointsDecodePool.post(id, msg)   -- hand a node's COMPRESSED blobs to a worker
//   __pointsDecodePool.drain()         -- pull every finished reply since the last call (once per frame)
//
// `post`'s msg.buffers are Uint8Array views straight into the render module's wasm heap; we slice() them into
// standalone ArrayBuffers (never transfer a heap view — that would detach the render heap) and hand those to
// a round-robin worker as Transferables. Replies come back as Transferable ArrayBuffers and queue up until
// the render module drains them. See decodeWorker.ts for the worker side and the C++ loader for the caller.

// The decode payload the C++ loader posts (buffers are heap views at call time).
interface DecodePostMessage {
  treeScale: number;
  treeOffset: [number, number, number] | { [i: number]: number };
  formats: { type: number; components: number }[];
  buffers: (Uint8Array | null)[];
  wantSalvage?: boolean;
}

// One finished decode, keyed so the render module can match it to the request handle it issued.
interface DecodeReply {
  id: number;
  result: unknown;
}

export interface DecodeWorkerPool {
  post(id: number, msg: DecodePostMessage): void;
  drain(): DecodeReply[];
  dispose(): void;
}

class DecodeWorkerPoolImpl implements DecodeWorkerPool {
  private readonly workers: Worker[] = [];
  private completed: DecodeReply[] = [];
  private next = 0;

  constructor(size: number) {
    for (let i = 0; i < size; i++) {
      // Vite resolves this to a bundled, code-split module worker in both dev and build.
      const w = new Worker(new URL('./decodeWorker.ts', import.meta.url), { type: 'module' });
      w.onmessage = (ev: MessageEvent) => {
        const { id, ...result } = ev.data as { id: number };
        this.completed.push({ id, result });
      };
      w.onerror = (e) => console.error('[decode-worker] error', e.message ?? e);
      this.workers.push(w);
    }
  }

  post(id: number, msg: DecodePostMessage): void {
    const transfer: ArrayBuffer[] = [];
    const buffers = msg.buffers.map((b) => {
      if (!b) return null;
      const copy = b.slice(); // copy out of the render module's heap so we can transfer + never detach it
      transfer.push(copy.buffer);
      return copy;
    });
    const off = msg.treeOffset;
    const req = {
      id,
      treeScale: msg.treeScale,
      treeOffset: [off[0], off[1], off[2]] as [number, number, number],
      formats: [0, 1, 2, 3].map((i) => ({ type: msg.formats[i].type, components: msg.formats[i].components })),
      buffers,
      wantSalvage: msg.wantSalvage === true,
    };
    const w = this.workers[this.next];
    this.next = (this.next + 1) % this.workers.length;
    w.postMessage(req, transfer);
  }

  drain(): DecodeReply[] {
    if (this.completed.length === 0) return [];
    const out = this.completed;
    this.completed = [];
    return out;
  }

  dispose(): void {
    for (const w of this.workers) w.terminate();
    this.workers.length = 0;
    this.completed = [];
    if ((globalThis as unknown as { __pointsDecodePool?: unknown }).__pointsDecodePool === this) {
      delete (globalThis as unknown as { __pointsDecodePool?: unknown }).__pointsDecodePool;
    }
  }
}

/**
 * Install a decode-worker pool as globalThis.__pointsDecodePool (idempotent — returns the existing one if
 * already installed). The wasm render module picks it up when a data source is created; without it, decode
 * falls back to the main thread. Returns null if Web Workers are unavailable.
 */
export function installDecodeWorkerPool(size?: number): DecodeWorkerPool | null {
  const g = globalThis as unknown as { __pointsDecodePool?: DecodeWorkerPool };
  if (g.__pointsDecodePool) return g.__pointsDecodePool;
  if (typeof Worker === 'undefined') return null;
  const n = size ?? Math.max(1, Math.min(4, (navigator.hardwareConcurrency || 4) - 1));
  const pool = new DecodeWorkerPoolImpl(n);
  g.__pointsDecodePool = pool;
  return pool;
}
