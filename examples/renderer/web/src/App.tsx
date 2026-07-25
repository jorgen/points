import { useEffect, useMemo, useState } from 'react';
import { ConnectForm, buildConnection, type FormValues } from './ConnectForm';
import { Viewer } from './Viewer';
import type { Connection } from './pointsRender';

// The default demo dataset: a small synthetic point cloud in a public S3 bucket. It is prefilled (and
// auto-connected) so the page renders something immediately with no input; the connect form is still there
// for pointing at another dataset, with credentials for a private bucket.
const DEFAULT_URL = 's3://limilind-public/points/synthetic';
const DEFAULT_CONNECTION = 'region=eu-north-1';

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

  // Optional one-shot auto-connect from URL params (for shareable links / headless testing).
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

  // `busy` only reflects the brief window until the Viewer takes over; clear it once a connection is set.
  useEffect(() => {
    if (connection) setBusy(false);
  }, [connection]);

  return (
    <div className="app">
      <aside className="sidebar">
        <header className="sidebar__header">
          <h1>points</h1>
          <p>WebGL2 · WebAssembly renderer</p>
        </header>
        <ConnectForm
          initial={initial}
          connected={connection !== null}
          busy={busy}
          onConnect={onConnect}
          onDisconnect={onDisconnect}
        />
        <footer className="sidebar__footer">
          Streams &amp; decodes an octree point cloud from S3, drawn on-demand in WebGL2.
        </footer>
      </aside>
      <main className="content">
        <Viewer connection={connection} />
      </main>
    </div>
  );
}
