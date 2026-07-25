import { useEffect, useMemo, useState } from 'react';
import { ConnectForm, buildConnection, type FormValues } from './ConnectForm';
import { Viewer } from './Viewer';
import type { Connection } from './pointsRender';

/**
 * Read connection defaults from the URL query string, e.g.
 *   ?url=s3://pointstest/synth&connection=<url-encoded connection string>&autoconnect=1
 * `connection` is the connection string (CLI grammar). autoconnect connects immediately if the dataset URL
 * is present. NOTE: passing secrets in the URL is convenient for local testing only — never do it against a
 * real/shared deployment.
 */
function parseParams(): { initial: Partial<FormValues>; autoconnect: boolean } {
  const p = new URLSearchParams(window.location.search);
  const str = (k: string) => p.get(k) ?? undefined;
  const initial: Partial<FormValues> = {
    url: str('url'),
    connectionString: str('connection'),
  };
  const autoconnect = ['1', 'true'].includes(p.get('autoconnect') ?? '');
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
