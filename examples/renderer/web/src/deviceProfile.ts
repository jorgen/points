/**
 * Device-class detection for memory-sensitive renderer defaults. Mobile browsers kill a tab whose wasm
 * heap grows past ~1-1.5GB (iOS Safari well below that), and the emscripten heap never shrinks — so phones
 * must start with much smaller streaming budgets than desktops. The user can still override every value
 * in the Controls panel.
 */
export interface DeviceProfile {
  isMobile: boolean;
  /** Total CPU-memory budget (MB) handed to Renderer.setMemoryBudgetMb. */
  memoryBudgetMb: number;
  /** GPU buffer budget (MB) — GL memory, outside the wasm heap. */
  gpuBudgetMb: number;
  /** Max concurrent tile requests. */
  maxInFlightIo: number;
  /** Decode-worker pool size (each worker owns an independent wasm heap). */
  decodeWorkers: number;
}

export function detectDeviceProfile(): DeviceProfile {
  const nav = navigator as Navigator & { deviceMemory?: number };
  // deviceMemory: Chrome/Android only (undefined on iOS/Firefox). <=4GB devices get the mobile profile
  // even with a desktop UA. maxTouchPoints>1 + /Mac/ catches iPadOS masquerading as macOS Safari.
  const uaMobile = /Mobi|Android|iPhone|iPad/i.test(navigator.userAgent);
  const touchDevice = navigator.maxTouchPoints > 1;
  const lowMem = nav.deviceMemory !== undefined && nav.deviceMemory <= 4;
  const isMobile = lowMem || (uaMobile && touchDevice) || (touchDevice && /Mac/.test(navigator.userAgent));
  return isMobile
    ? { isMobile, memoryBudgetMb: 256, gpuBudgetMb: 160, maxInFlightIo: 16, decodeWorkers: 2 }
    : {
        isMobile,
        memoryBudgetMb: 1024,
        gpuBudgetMb: 512,
        maxInFlightIo: 64,
        decodeWorkers: Math.max(1, Math.min(4, (navigator.hardwareConcurrency || 4) - 1)),
      };
}
