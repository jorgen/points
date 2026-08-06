import { useEffect, useMemo, useRef, useState } from 'react';
import { ConnectForm, PUBLIC_CONNECTION, PUBLIC_DATASETS, buildConnection, type FormValues } from './ConnectForm';
import { Controls } from './Controls';
import { Viewer } from './Viewer';
import { usePointCloudRenderer } from './usePointCloudRenderer';
import type { Connection } from './dewRender';

// The default demo dataset: a public point cloud in an S3 bucket. It is prefilled (and auto-connected) so
// the page renders something immediately with no input; the connect form is still there for pointing at
// another dataset, with credentials for a private bucket.
const DEFAULT_URL = PUBLIC_DATASETS[0].url;
const DEFAULT_CONNECTION = PUBLIC_CONNECTION;

/**
 * Read connection defaults from the URL query string, e.g.
 *   ?url=s3://bucket/prefix&connection=<url-encoded connection string>&autoconnect=1
 * `connection` is the connection string (CLI grammar). Absent params fall back to the public demo dataset,
 * and the page auto-connects on open unless ?autoconnect=0 is passed. NOTE: passing secrets in the URL is
 * convenient for local testing only — never do it against a real/shared deployment.
 */
function parseParams(): { initial: Partial<FormValues>; autoconnect: boolean } {
  const p = new URLSearchParams(window.location.search);
  const str = (k: string) => p.get(k) ?? undefined;
  const initial: Partial<FormValues> = {
    url: str('url') ?? DEFAULT_URL,
    connectionString: str('connection') ?? DEFAULT_CONNECTION,
  };
  // Auto-connect by default so the demo dataset loads on open; disable with ?autoconnect=0.
  const ac = p.get('autoconnect');
  const autoconnect = ac === null ? true : ['1', 'true'].includes(ac);
  return { initial, autoconnect };
}

export function App() {
  const { initial, autoconnect } = useMemo(parseParams, []);
  const [connection, setConnection] = useState<Connection | null>(null);
  const [busy, setBusy] = useState(false);
  // The panel starts open on a wide screen and collapsed on a phone, so the canvas gets full width.
  const [panelOpen, setPanelOpen] = useState(() => (typeof window === 'undefined' ? true : window.innerWidth > 760));

  // The canvas + renderer lifecycle live here (not in Viewer) so the controls can sit in the sidebar.
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const renderer = usePointCloudRenderer(canvasRef, connection);

  // Optional one-shot auto-connect from URL params / the demo default.
  useEffect(() => {
    if (!autoconnect) return;
    const c = buildConnection(initial);
    if (c) setConnection(c);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const onConnect = (c: Connection) => {
    setBusy(true);
    setConnection(c);
  };
  const onDisconnect = () => {
    setBusy(false);
    setConnection(null);
  };

  // `busy` only reflects the brief window until the renderer takes over; clear it once a connection is set.
  useEffect(() => {
    if (connection) setBusy(false);
  }, [connection]);

  return (
    <div className={panelOpen ? 'app' : 'app app--collapsed'}>
      <aside className="sidebar">
        <header className="sidebar__header">
          <div>
            <h1>points</h1>
            <p>WebGL2 · WebAssembly renderer</p>
          </div>
          <button type="button" className="icon-btn" aria-label="Collapse panel" onClick={() => setPanelOpen(false)}>
            ✕
          </button>
        </header>

        <ConnectForm
          initial={initial}
          connected={connection !== null}
          busy={busy}
          onConnect={onConnect}
          onDisconnect={onDisconnect}
        />

        {renderer.status === 'ready' && (
          <Controls
            attributes={renderer.attributes}
            activeAttribute={renderer.activeAttribute}
            setActiveAttribute={renderer.setActiveAttribute}
            controls={renderer.controls}
            setControl={renderer.setControl}
            getVirtualStats={renderer.getVirtualStats}
            getMemoryStats={renderer.getMemoryStats}
            resetView={renderer.resetView}
            pointsRendered={renderer.pointsRendered}
            aabb={renderer.aabb}
          />
        )}

        <footer className="sidebar__footer">
          Streams &amp; decodes an octree point cloud from S3, drawn on-demand in WebGL2.
        </footer>
      </aside>

      <main className="content">
        {!panelOpen && (
          <button type="button" className="panel-open" aria-label="Open controls" onClick={() => setPanelOpen(true)}>
            ☰
          </button>
        )}
        <Viewer canvasRef={canvasRef} status={renderer.status} error={renderer.error} />
      </main>

      {/* Mobile only (CSS-gated): tap the backdrop to dismiss the drawer. */}
      {panelOpen && <div className="backdrop" onClick={() => setPanelOpen(false)} />}
    </div>
  );
}
