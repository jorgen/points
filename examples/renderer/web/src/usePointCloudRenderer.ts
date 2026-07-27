import { useCallback, useEffect, useRef, useState, type RefObject } from 'react';
import { loadPointsRender, type Aabb, type Connection, type Renderer } from './pointsRender';

/** DOM id of the canvas the renderer draws into (used as the Emscripten WebGL context selector). */
export const CANVAS_ID = 'points-cloud-canvas';

export type RendererStatus = 'idle' | 'connecting' | 'ready' | 'error';

/** Live view controls, mirroring the desktop app's Input panel. */
export interface ViewControls {
  /** Point splat world size. */
  pointSize: number;
  /** Level-of-detail scale base (higher keeps coarser LOD). */
  lodScaleBase: number;
  /** Octree refinement budget: smaller = more detail (+ more streaming). */
  pixelErrorThreshold: number;
  /** Runtime per-node LOD: on-screen spacing (px) between drawn points → uniform density. Higher = sparser. */
  renderDensityPx: number;
  /** GPU memory budget in MB. */
  gpuMemoryBudgetMb: number;
  /** Per-node bounding-box overlay. */
  showBoundingBoxes: boolean;
}

// Defaults match the C++ gl_renderer fields (point_world_size 0.05, lod_scale_base 1.1); the streaming
// values are browser-appropriate starting points pushed to the data source on connect.
export const DEFAULT_CONTROLS: ViewControls = {
  pointSize: 0.05,
  lodScaleBase: 1.1,
  pixelErrorThreshold: 1.0,
  renderDensityPx: 1.5,
  gpuMemoryBudgetMb: 512,
  showBoundingBoxes: false,
};

function applyControls(r: Renderer, c: ViewControls) {
  r.setPointSize(c.pointSize);
  r.setLodScaleBase(c.lodScaleBase);
  r.setPixelErrorThreshold(c.pixelErrorThreshold);
  r.setRenderDensityPx(c.renderDensityPx);
  r.setGpuMemoryBudgetMb(c.gpuMemoryBudgetMb);
  r.setShowBoundingBoxes(c.showBoundingBoxes);
}

export interface RendererState {
  status: RendererStatus;
  error: string | null;
  attributes: string[];
  activeAttribute: string | null;
  setActiveAttribute: (name: string) => void;
  pointsRendered: number;
  aabb: Aabb | null;
  controls: ViewControls;
  setControl: <K extends keyof ViewControls>(key: K, value: ViewControls[K]) => void;
  resetView: () => void;
}

// Wheel deltas differ by device/mode; normalize a notch to roughly this zoom step.
const WHEEL_ZOOM_SCALE = 0.0015;

/**
 * Choose the default color attribute by precedence: 'rgb', then 'intensity', then the first usable
 * attribute after the coordinates (i.e. not 'xyz'), falling back to 'xyz' (the first name) if none of the
 * above exist. Matching is case-insensitive. getAttributeNames() lists coordinates ('xyz') first.
 */
function pickDefaultAttribute(names: string[]): string | null {
  if (names.length === 0) return null;
  const lower = names.map((n) => n.toLowerCase());
  const byName = (target: string) => {
    const i = lower.indexOf(target);
    return i >= 0 ? names[i] : null;
  };
  return byName('rgb') ?? byName('intensity') ?? names.find((n) => n.toLowerCase() !== 'xyz') ?? names[0];
}

/**
 * Owns the lifecycle of a single renderer instance bound to `canvasRef` + `connection`:
 * loads the module, creates the renderer, wires the DIRTY-DRIVEN render loop (the WASM calls back into
 * `requestRedraw`, coalesced into one requestAnimationFrame), maps pointer/wheel input to the semantic
 * camera API, tracks device-pixel-ratio resizes, and disposes everything on change/unmount.
 */
export function usePointCloudRenderer(
  canvasRef: RefObject<HTMLCanvasElement | null>,
  connection: Connection | null,
): RendererState {
  const [status, setStatus] = useState<RendererStatus>('idle');
  const [error, setError] = useState<string | null>(null);
  const [attributes, setAttributes] = useState<string[]>([]);
  const [activeAttribute, setActiveAttributeState] = useState<string | null>(null);
  const [pointsRendered, setPointsRendered] = useState(0);
  const [aabb, setAabb] = useState<Aabb | null>(null);
  const [controls, setControlsState] = useState<ViewControls>(DEFAULT_CONTROLS);
  const rendererRef = useRef<Renderer | null>(null);
  // Latest controls for the connect effect to apply without re-running on every tweak.
  const controlsRef = useRef(controls);
  controlsRef.current = controls;

  useEffect(() => {
    if (!connection) {
      setStatus('idle');
      return;
    }
    const canvas = canvasRef.current;
    if (!canvas) {
      setStatus('error');
      setError('canvas element is not mounted');
      return;
    }

    let cancelled = false;
    const cleanups: Array<() => void> = [];
    setStatus('connecting');
    setError(null);

    void (async () => {
      let renderer: Renderer | null = null;
      try {
        const mod = await loadPointsRender();
        if (cancelled) return;

        renderer = await mod.createRenderer('#' + CANVAS_ID, connection.url, connection.connectionString);
        if (cancelled || !renderer) {
          renderer?.dispose();
          if (!cancelled && !renderer) {
            setStatus('error');
            setError('createRenderer returned null — check the browser console for details');
          }
          return;
        }
        rendererRef.current = renderer;
        const r = renderer;

        // Read initial state up front — these embind calls are the only realistic throw sites, so if one
        // throws nothing below has been wired yet and the catch only has to dispose.
        const names = r.getAttributeNames();
        const bounds = r.getAabb();
        // Apply the current view controls to the fresh renderer so the UI and renderer agree from the
        // first frame (and a reconnect preserves the user's settings).
        applyControls(r, controlsRef.current);

        // --- dirty-driven render loop: coalesce redraw requests into a single rAF ---
        let raf = 0;
        const drawingBufferSize = () => {
          const dpr = window.devicePixelRatio || 1;
          return {
            w: Math.max(1, Math.round(canvas.clientWidth * dpr)),
            h: Math.max(1, Math.round(canvas.clientHeight * dpr)),
          };
        };
        const requestRedraw = () => {
          if (raf) return;
          raf = requestAnimationFrame(() => {
            raf = 0;
            const { w, h } = drawingBufferSize();
            if (canvas.width !== w || canvas.height !== h) {
              canvas.width = w;
              canvas.height = h;
            }
            r.frame(w, h);
            const p = r.getPointsRendered();
            setPointsRendered((prev) => (prev === p ? prev : p));
          });
        };
        r.setRequestUpdate(requestRedraw);
        cleanups.push(() => {
          if (raf) cancelAnimationFrame(raf);
        });

        // --- react to CSS-box resizes and device-pixel-ratio changes ---
        const ro = new ResizeObserver(() => requestRedraw());
        ro.observe(canvas);
        cleanups.push(() => ro.disconnect());
        // ResizeObserver ignores pure device-pixel-ratio changes (e.g. moving the window between a 1x
        // and a 2x display); watch the resolution media query and re-arm it for each new ratio.
        let dprQuery: MediaQueryList | null = null;
        const onDprChange = () => {
          requestRedraw();
          armDpr();
        };
        const armDpr = () => {
          dprQuery?.removeEventListener('change', onDprChange);
          dprQuery = window.matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`);
          dprQuery.addEventListener('change', onDprChange);
        };
        armDpr();
        cleanups.push(() => dprQuery?.removeEventListener('change', onDprChange));

        // --- input -> semantic camera (deltas normalized by canvas CSS size). Mouse/pen drive orbit/pan/
        //     dolly by button; touch is gesture-based: one finger orbits, two fingers pinch to zoom and
        //     drag to move the arcball center. ---
        const cssSize = () => ({ w: canvas.clientWidth || 1, h: canvas.clientHeight || 1 });

        // Single-pointer drag state — used by mouse/pen and by one-finger touch orbit.
        let dragging = false;
        let activeButton = 0;
        let activePointer = -1;
        let lastX = 0;
        let lastY = 0;

        // Active touch points, keyed by pointerId (insertion order preserved so the first two drive a
        // gesture). `pinch` is the running reference (finger spacing + midpoint) while >=2 fingers are down.
        const touches = new Map<number, { x: number; y: number }>();
        let pinch: { dist: number; midX: number; midY: number } | null = null;
        const twoFinger = () => {
          const [a, b] = [...touches.values()].slice(0, 2);
          return { dist: Math.hypot(b.x - a.x, b.y - a.y), midX: (a.x + b.x) / 2, midY: (a.y + b.y) / 2 };
        };
        const beginOrbit = (id: number, x: number, y: number) => {
          dragging = true;
          activeButton = 0; // orbit
          activePointer = id;
          lastX = x;
          lastY = y;
        };

        const onPointerDown = (e: PointerEvent) => {
          if (e.pointerType === 'touch') {
            e.preventDefault();
            canvas.setPointerCapture(e.pointerId);
            touches.set(e.pointerId, { x: e.clientX, y: e.clientY });
            if (touches.size === 1) {
              beginOrbit(e.pointerId, e.clientX, e.clientY); // one finger orbits
            } else {
              dragging = false; // a second finger switches to the pinch/pan gesture
              activePointer = -1;
              pinch = twoFinger();
            }
            return;
          }
          // mouse / pen
          if (dragging) return; // ignore extra buttons while a drag is active
          if (e.button !== 0 && e.button !== 1 && e.button !== 2) return;
          e.preventDefault(); // suppress middle-click autoscroll / incidental selection
          dragging = true;
          activeButton = e.button;
          activePointer = e.pointerId;
          lastX = e.clientX;
          lastY = e.clientY;
          canvas.setPointerCapture(e.pointerId);
        };
        const onPointerMove = (e: PointerEvent) => {
          const { w, h } = cssSize();
          if (e.pointerType === 'touch') {
            if (!touches.has(e.pointerId)) return;
            touches.set(e.pointerId, { x: e.clientX, y: e.clientY });
            if (touches.size >= 2 && pinch) {
              const m = twoFinger();
              // Pinch -> zoom: treat the change in finger spacing like a wheel deltaY (same tuned scale).
              // Spreading the fingers (dist grows) should zoom IN, and cameraZoom's negative arg shrinks
              // the orbit distance, so negate.
              const dDist = m.dist - pinch.dist;
              if (dDist !== 0) r.cameraZoom(-dDist * WHEEL_ZOOM_SCALE);
              // Two-finger drag -> move the arcball center in the view plane (same normalization as pan).
              const dx = (m.midX - pinch.midX) / w;
              const dy = (pinch.midY - m.midY) / h; // screen Y is down; arcball up is positive
              if (dx !== 0 || dy !== 0) r.cameraPan(dx, dy);
              pinch = m;
            } else if (dragging && e.pointerId === activePointer) {
              r.cameraRotate((e.clientX - lastX) / w, (lastY - e.clientY) / h); // one finger orbits
              lastX = e.clientX;
              lastY = e.clientY;
            }
            return;
          }
          // mouse / pen
          if (!dragging || e.pointerId !== activePointer) return;
          const dx = (e.clientX - lastX) / w;
          // Screen Y grows downward; negate so it matches the arcball's up-positive convention.
          const dy = (lastY - e.clientY) / h;
          lastX = e.clientX;
          lastY = e.clientY;
          if (activeButton === 0) {
            r.cameraRotate(dx, dy);
          } else if (activeButton === 2) {
            if (e.ctrlKey) r.cameraPanGround(dx, dy); // ctrl+right-drag = pan in the ground plane
            else if (e.shiftKey) r.cameraDolly(dy);
            else r.cameraPan(dx, dy);
          } else {
            r.cameraPan(dx, dy); // middle button
          }
        };
        const endPointer = (e: PointerEvent) => {
          try {
            canvas.releasePointerCapture(e.pointerId);
          } catch {
            // pointer may already be released
          }
          if (e.pointerType === 'touch') {
            touches.delete(e.pointerId);
            if (touches.size >= 2) {
              pinch = twoFinger(); // lost one of >2 fingers: re-anchor so the remaining pair doesn't jump
            } else if (touches.size === 1) {
              pinch = null;
              const [id, p] = [...touches.entries()][0];
              beginOrbit(id, p.x, p.y); // drop back to one-finger orbit without a jump
            } else {
              pinch = null;
              dragging = false;
              activePointer = -1;
            }
            return;
          }
          // mouse / pen
          if (e.pointerId !== activePointer) return;
          dragging = false;
          activePointer = -1;
        };
        const onWheel = (e: WheelEvent) => {
          e.preventDefault();
          // deltaMode 0 = pixels, 1 = lines, 2 = pages — bring them to a comparable scale. DOM deltaY is
          // positive scrolling down; a negative zoom arg shrinks the orbit distance, so the sign passes
          // straight through (scroll up => zoom in).
          const unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? canvas.clientHeight || 600 : 1;
          r.cameraZoom(e.deltaY * unit * WHEEL_ZOOM_SCALE);
        };
        const onContextMenu = (e: Event) => e.preventDefault();

        canvas.addEventListener('pointerdown', onPointerDown);
        canvas.addEventListener('pointermove', onPointerMove);
        canvas.addEventListener('pointerup', endPointer);
        canvas.addEventListener('pointercancel', endPointer);
        canvas.addEventListener('wheel', onWheel, { passive: false });
        canvas.addEventListener('contextmenu', onContextMenu);
        cleanups.push(() => {
          canvas.removeEventListener('pointerdown', onPointerDown);
          canvas.removeEventListener('pointermove', onPointerMove);
          canvas.removeEventListener('pointerup', endPointer);
          canvas.removeEventListener('pointercancel', endPointer);
          canvas.removeEventListener('wheel', onWheel);
          canvas.removeEventListener('contextmenu', onContextMenu);
        });

        // --- publish initial UI state + first draw ---
        setAttributes(names);
        // Pick a sensible default color attribute by precedence rather than always the first (xyz):
        // rgb, then intensity, then the first usable non-coordinate attribute, falling back to xyz.
        const preferred = pickDefaultAttribute(names);
        if (preferred) r.setAttribute(preferred);
        setActiveAttributeState(preferred);
        setAabb(bounds);
        setStatus('ready');
        requestRedraw();
      } catch (err) {
        if (!cancelled) {
          setStatus('error');
          setError(err instanceof Error ? err.message : String(err));
        }
        for (const c of cleanups) c(); // tear down anything already wired before the throw
        cleanups.length = 0;
        renderer?.dispose();
        rendererRef.current = null;
      }
    })();

    return () => {
      cancelled = true;
      for (const c of cleanups) c();
      const r = rendererRef.current;
      rendererRef.current = null;
      r?.dispose(); // dispose() clears the wake hook first, so a late fetch cannot call a stale redraw
      setStatus('idle');
      setPointsRendered(0);
      setAttributes([]);
      setActiveAttributeState(null);
      setAabb(null);
    };
  }, [connection, canvasRef]);

  const setActiveAttribute = useCallback((name: string) => {
    const r = rendererRef.current;
    if (!r) return;
    r.setAttribute(name); // marks the frame dirty -> requestRedraw fires via the wake hook
    setActiveAttributeState(name);
  }, []);

  // Update one control: reflect it in React state and push it to the renderer (which marks dirty).
  const setControl = useCallback(<K extends keyof ViewControls>(key: K, value: ViewControls[K]) => {
    setControlsState((prev) => ({ ...prev, [key]: value }));
    const r = rendererRef.current;
    if (!r) return;
    switch (key) {
      case 'pointSize':
        r.setPointSize(value as number);
        break;
      case 'lodScaleBase':
        r.setLodScaleBase(value as number);
        break;
      case 'pixelErrorThreshold':
        r.setPixelErrorThreshold(value as number);
        break;
      case 'renderDensityPx':
        r.setRenderDensityPx(value as number);
        break;
      case 'gpuMemoryBudgetMb':
        r.setGpuMemoryBudgetMb(value as number);
        break;
      case 'showBoundingBoxes':
        r.setShowBoundingBoxes(value as boolean);
        break;
    }
  }, []);

  const resetView = useCallback(() => rendererRef.current?.resetView(), []);

  return {
    status,
    error,
    attributes,
    activeAttribute,
    setActiveAttribute,
    pointsRendered,
    aabb,
    controls,
    setControl,
    resetView,
  };
}
