import { useRef } from 'react';
import { CANVAS_ID, usePointCloudRenderer, type RendererStatus } from './usePointCloudRenderer';
import type { Aabb, Connection } from './pointsRender';

const fmtVec = (v: [number, number, number]) => `[${v.map((x) => x.toFixed(0)).join(', ')}]`;

function StatusLine({ status, error }: { status: RendererStatus; error: string | null }) {
  if (status === 'connecting') return <div className="badge badge--busy">Connecting…</div>;
  if (status === 'error') return <div className="badge badge--error">Error: {error}</div>;
  return null;
}

function BoundsLine({ aabb }: { aabb: Aabb }) {
  return (
    <div className="hud__bounds">
      bounds {fmtVec(aabb.min)} → {fmtVec(aabb.max)}
    </div>
  );
}

export function Viewer({ connection }: { connection: Connection | null }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const { status, error, attributes, activeAttribute, setActiveAttribute, pointsRendered, aabb } =
    usePointCloudRenderer(canvasRef, connection);

  return (
    <div className="viewer">
      <canvas id={CANVAS_ID} ref={canvasRef} className="viewer__canvas" />

      <div className="viewer__overlay">
        <StatusLine status={status} error={error} />

        {status === 'ready' && (
          <div className="hud">
            <label className="hud__row">
              <span>Attribute</span>
              <select value={activeAttribute ?? ''} onChange={(e) => setActiveAttribute(e.target.value)}>
                {attributes.map((name) => (
                  <option key={name} value={name}>
                    {name}
                  </option>
                ))}
              </select>
            </label>
            <div className="hud__row">
              <span>Points</span>
              <b>{pointsRendered.toLocaleString()}</b>
            </div>
            {aabb && <BoundsLine aabb={aabb} />}
          </div>
        )}
      </div>

      {status === 'idle' && (
        <div className="viewer__hint">
          <p>Connect to an S3 dataset to start rendering.</p>
          <p className="viewer__hint-sub">
            Drag to orbit · right-drag to pan · shift+right-drag to dolly · scroll to zoom
          </p>
        </div>
      )}
    </div>
  );
}
