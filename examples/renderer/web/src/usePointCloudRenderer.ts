import { useCallback, useEffect, useRef, useState, type RefObject } from 'react';
import { loadPointsRender, type Aabb, type Connection, type Renderer } from './pointsRender';

/** DOM id of the canvas the renderer draws into (used as the Emscripten WebGL context selector). */
export const CANVAS_ID = 'points-cloud-canvas';

export type RendererStatus = 'idle' | 'connecting' | 'ready' | 'error';

export interface RendererState {
  status: RendererStatus;
  error: string | null;
  attributes: string[];
  activeAttribute: string | null;
  setActiveAttribute: (name: string) => void;
  pointsRendered: number;
  aabb: Aabb | null;
}

// Wheel deltas differ by device/mode; normalize a notch to roughly this zoom step.
const WHEEL_ZOOM_SCALE = 0.0015;

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
  const rendererRef = useRef<Renderer | null>(null);

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

        // --- pointer/wheel input -> semantic camera (deltas normalized by canvas CSS size) ---
        let dragging = false;
        let activeButton = 0;
        let activePointer = -1;
        let lastX = 0;
        let lastY = 0;
        const cssSize = () => ({ w: canvas.clientWidth || 1, h: canvas.clientHeight || 1 });

        const onPointerDown = (e: PointerEvent) => {
          if (dragging) return; // ignore extra pointers while a drag is active
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
          if (!dragging || e.pointerId !== activePointer) return;
          const { w, h } = cssSize();
          const dx = (e.clientX - lastX) / w;
          // Screen Y grows downward; negate so it matches the arcball's up-positive convention.
          const dy = (lastY - e.clientY) / h;
          lastX = e.clientX;
          lastY = e.clientY;
          if (activeButton === 0) {
            r.cameraRotate(dx, dy);
          } else if (activeButton === 2) {
            if (e.shiftKey) r.cameraDolly(dy);
            else r.cameraPan(dx, dy);
          } else {
            r.cameraPan(dx, dy); // middle button
          }
        };
        const endDrag = (e: PointerEvent) => {
          if (e.pointerId !== activePointer) return;
          dragging = false;
          activePointer = -1;
          try {
            canvas.releasePointerCapture(e.pointerId);
          } catch {
            // pointer may already be released
          }
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
        canvas.addEventListener('pointerup', endDrag);
        canvas.addEventListener('pointercancel', endDrag);
        canvas.addEventListener('wheel', onWheel, { passive: false });
        canvas.addEventListener('contextmenu', onContextMenu);
        cleanups.push(() => {
          canvas.removeEventListener('pointerdown', onPointerDown);
          canvas.removeEventListener('pointermove', onPointerMove);
          canvas.removeEventListener('pointerup', endDrag);
          canvas.removeEventListener('pointercancel', endDrag);
          canvas.removeEventListener('wheel', onWheel);
          canvas.removeEventListener('contextmenu', onContextMenu);
        });

        // --- publish initial UI state + first draw ---
        setAttributes(names);
        setActiveAttributeState(names[0] ?? null);
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

  return { status, error, attributes, activeAttribute, setActiveAttribute, pointsRendered, aabb };
}
