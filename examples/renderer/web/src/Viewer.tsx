import type { RefObject } from 'react';
import { CANVAS_ID, type RendererStatus } from './usePointCloudRenderer';

function StatusLine({ status, error }: { status: RendererStatus; error: string | null }) {
  if (status === 'connecting') return <div className="badge badge--busy">Connecting…</div>;
  if (status === 'error') return <div className="badge badge--error">Error: {error}</div>;
  return null;
}

/** Hosts the WebGL2 <canvas> the renderer draws into, plus status + first-run hint overlays. The renderer
 * lifecycle + controls live in App (via usePointCloudRenderer) so the controls can sit in the sidebar. */
export function Viewer({
  canvasRef,
  status,
  error,
}: {
  canvasRef: RefObject<HTMLCanvasElement>;
  status: RendererStatus;
  error: string | null;
}) {
  return (
    <div className="viewer">
      <canvas id={CANVAS_ID} ref={canvasRef} className="viewer__canvas" />

      <div className="viewer__overlay">
        <StatusLine status={status} error={error} />
      </div>

      {status === 'idle' && (
        <div className="viewer__hint">
          <p>Connect to a dataset to start rendering.</p>
          <p className="viewer__hint-sub">
            Drag to orbit · right-drag to pan · ctrl+right to pan on the ground · shift+right to dolly · scroll to
            zoom · one finger to orbit · pinch to zoom · two-finger drag to move
          </p>
        </div>
      )}
    </div>
  );
}
