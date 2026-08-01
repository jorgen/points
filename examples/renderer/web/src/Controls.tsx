import { useEffect, useState } from 'react';
import type { Aabb, MemoryStats } from './dewRender';
import type { ViewControls, VirtualStats } from './usePointCloudRenderer';

interface ControlsProps {
  attributes: string[];
  activeAttribute: string | null;
  setActiveAttribute: (name: string) => void;
  controls: ViewControls;
  setControl: <K extends keyof ViewControls>(key: K, value: ViewControls[K]) => void;
  getVirtualStats: () => VirtualStats | null;
  getMemoryStats: () => MemoryStats | null;
  resetView: () => void;
  pointsRendered: number;
  aabb: Aabb | null;
}

function Slider({
  label,
  title,
  value,
  min,
  max,
  step,
  onChange,
  format,
}: {
  label: string;
  title?: string;
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (v: number) => void;
  format: (v: number) => string;
}) {
  return (
    <label className="ctl" title={title}>
      <span className="ctl__label">
        {label}
        <b>{format(value)}</b>
      </span>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(e.currentTarget.valueAsNumber)}
      />
    </label>
  );
}

const fmtVec = (v: [number, number, number]) => `[${v.map((x) => x.toFixed(0)).join(', ')}]`;
const fmtMb = (bytes: number) => `${(bytes / (1024 * 1024)).toFixed(1)} MB`;

/** The live view controls, ported from the desktop app's "Input" panel. Lives in the sidebar. */
export function Controls({
  attributes,
  activeAttribute,
  setActiveAttribute,
  controls,
  setControl,
  getVirtualStats,
  getMemoryStats,
  resetView,
  pointsRendered,
  aabb,
}: ControlsProps) {
  // Poll virtual-subnode + memory telemetry a couple of times a second for the diagnostics panel.
  const [vstats, setVstats] = useState<VirtualStats | null>(null);
  const [mstats, setMstats] = useState<MemoryStats | null>(null);
  useEffect(() => {
    const id = window.setInterval(() => {
      setVstats(getVirtualStats());
      setMstats(getMemoryStats());
    }, 500);
    return () => window.clearInterval(id);
  }, [getVirtualStats, getMemoryStats]);

  return (
    <div className="controls">
      <section className="controls__section">
        <h2>Camera</h2>
        <button type="button" className="btn btn--ghost" onClick={resetView}>
          Reset view
        </button>
        <p className="controls__hint">
          Drag orbit · right-drag pan · ctrl+right pan-ground · shift+right dolly · scroll zoom · 1 finger orbit ·
          pinch zoom · 2-finger drag move
        </p>
      </section>

      <section className="controls__section">
        <h2>Appearance</h2>
        <label className="ctl">
          <span className="ctl__label">Attribute</span>
          <select value={activeAttribute ?? ''} onChange={(e) => setActiveAttribute(e.target.value)}>
            {attributes.map((name) => (
              <option key={name} value={name}>
                {name}
              </option>
            ))}
          </select>
        </label>
        <Slider
          label="Point size"
          value={controls.pointSize}
          min={0.005}
          max={0.5}
          step={0.005}
          onChange={(v) => setControl('pointSize', v)}
          format={(v) => v.toFixed(3)}
        />
        <Slider
          label="LOD scale"
          value={controls.lodScaleBase}
          min={1.0}
          max={5.0}
          step={0.05}
          onChange={(v) => setControl('lodScaleBase', v)}
          format={(v) => v.toFixed(2)}
        />
      </section>

      <section className="controls__section">
        <h2>Detail</h2>
        <Slider
          label="Density (px, lower = denser)"
          title="On-screen spacing between drawn points WITHIN a node. Lower = denser. Draw-time thinning only — no extra streaming or GPU memory."
          value={controls.renderDensityPx}
          min={0.5}
          max={6.0}
          step={0.1}
          onChange={(v) => setControl('renderDensityPx', v)}
          format={(v) => `${v.toFixed(1)}px`}
        />
        <Slider
          label="Pixel error (lower = finer)"
          title="Octree refinement — WHICH nodes stream in (how deep the tree is walked). Lower = finer detail, but more IO and GPU memory."
          value={controls.pixelErrorThreshold}
          min={0.1}
          max={3.0}
          step={0.05}
          onChange={(v) => setControl('pixelErrorThreshold', v)}
          format={(v) => v.toFixed(2)}
        />
      </section>

      <section className="controls__section">
        <h2>Streaming</h2>
        <Slider
          label="GPU budget"
          value={controls.gpuMemoryBudgetMb}
          min={64}
          max={2048}
          step={64}
          onChange={(v) => setControl('gpuMemoryBudgetMb', v)}
          format={(v) => `${v} MB`}
        />
        <Slider
          label="Memory budget"
          title="Total CPU (wasm-heap) budget for streaming: read cache + decode backlog + virtual residents. Note: once the heap-pressure brake has latched (see Stats), raising this needs a page reload — the wasm heap never shrinks."
          value={controls.memoryBudgetMb}
          min={128}
          max={1024}
          step={64}
          onChange={(v) => setControl('memoryBudgetMb', v)}
          format={(v) => `${v} MB`}
        />
        <Slider
          label="Upload / frame"
          title="Per-frame GPU upload budget. Higher = refinement converges faster after a camera move (matters most on slow networks)."
          value={controls.uploadBudgetMb}
          min={1}
          max={64}
          step={1}
          onChange={(v) => setControl('uploadBudgetMb', v)}
          format={(v) => `${v} MB`}
        />
        <Slider
          label="Max in-flight IO"
          title="Maximum concurrent tile requests. Higher = faster convergence on high-latency (S3) connections."
          value={controls.maxInFlightIo}
          min={8}
          max={256}
          step={8}
          onChange={(v) => setControl('maxInFlightIo', v)}
          format={(v) => `${v}`}
        />
      </section>

      <section className="controls__section">
        <h2>Scene</h2>
        <label
          className="ctl ctl--check"
          title="Render-time balanced LOD for sparse 'spanning' leaves (near+far in one octree node). Off = those leaves fall back to their full-resolution monolith. Toggle to A/B compare on one view."
        >
          <input
            type="checkbox"
            checked={controls.enableVirtualSubtrees}
            onChange={(e) => setControl('enableVirtualSubtrees', e.currentTarget.checked)}
          />
          <span>Virtual subnodes (spanning-leaf LOD)</span>
        </label>
        <label className="ctl ctl--check">
          <input
            type="checkbox"
            checked={controls.showBoundingBoxes}
            onChange={(e) => setControl('showBoundingBoxes', e.currentTarget.checked)}
          />
          <span>Show node bounding boxes</span>
        </label>
      </section>

      <section className="controls__section">
        <h2>Stats</h2>
        <div className="ctl__stat">
          <span>Points</span>
          <b>{pointsRendered.toLocaleString()}</b>
        </div>
        {aabb && (
          <div className="ctl__stat ctl__stat--bounds">
            <span>Bounds</span>
            <b>
              {fmtVec(aabb.min)} → {fmtVec(aabb.max)}
            </b>
          </div>
        )}
        {vstats && (
          <>
            <div className="ctl__stat">
              <span>Promoted leaves</span>
              <b>{vstats.promoted.toLocaleString()}</b>
            </div>
            <div className="ctl__stat">
              <span>Virtual nodes drawn</span>
              <b>{vstats.nodesDrawn.toLocaleString()}</b>
            </div>
            <div className="ctl__stat">
              <span>Virtual GPU</span>
              <b>{fmtMb(vstats.gpuBytes)}</b>
            </div>
            <div className="ctl__stat">
              <span>Resident CPU</span>
              <b>{fmtMb(vstats.residentCpuBytes)}</b>
            </div>
          </>
        )}
        {mstats && (
          <>
            {mstats.heapMax > 0 && (
              <div className="ctl__stat" title="Current wasm heap vs its link-time ceiling. The heap never shrinks within a page load.">
                <span>Heap</span>
                <b>
                  {fmtMb(mstats.heapBytes)} / {fmtMb(mstats.heapMax)}
                </b>
              </div>
            )}
            <div className="ctl__stat" title="Estimated CPU bytes held by in-flight + decoded-awaiting-upload nodes last frame.">
              <span>Backlog</span>
              <b>{fmtMb(mstats.backlogBytes)}</b>
            </div>
            <div className="ctl__stat" title="Compressed read-cache occupancy.">
              <span>Read cache</span>
              <b>{fmtMb(mstats.readCacheBytes)}</b>
            </div>
            {mstats.brakeLevel > 0 && (
              <div className="ctl__stat" title="Heap-pressure brake: IO and cache caps are tightened to keep the heap under its ceiling. Latches until reload.">
                <span>Brake</span>
                <b>{mstats.brakeLevel >= 2 ? 'critical' : 'high'}</b>
              </div>
            )}
          </>
        )}
      </section>
    </div>
  );
}
