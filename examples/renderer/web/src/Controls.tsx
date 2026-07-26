import type { Aabb } from './pointsRender';
import type { ViewControls } from './usePointCloudRenderer';

interface ControlsProps {
  attributes: string[];
  activeAttribute: string | null;
  setActiveAttribute: (name: string) => void;
  controls: ViewControls;
  setControl: <K extends keyof ViewControls>(key: K, value: ViewControls[K]) => void;
  resetView: () => void;
  pointsRendered: number;
  aabb: Aabb | null;
}

function Slider({
  label,
  value,
  min,
  max,
  step,
  onChange,
  format,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (v: number) => void;
  format: (v: number) => string;
}) {
  return (
    <label className="ctl">
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

/** The live view controls, ported from the desktop app's "Input" panel. Lives in the sidebar. */
export function Controls({
  attributes,
  activeAttribute,
  setActiveAttribute,
  controls,
  setControl,
  resetView,
  pointsRendered,
  aabb,
}: ControlsProps) {
  return (
    <div className="controls">
      <section className="controls__section">
        <h2>Camera</h2>
        <button type="button" className="btn btn--ghost" onClick={resetView}>
          Reset view
        </button>
        <p className="controls__hint">
          Drag orbit · right-drag pan · ctrl+right pan-ground · shift+right dolly · scroll zoom
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
        <h2>Streaming</h2>
        <Slider
          label="Pixel error (lower = finer)"
          value={controls.pixelErrorThreshold}
          min={0.1}
          max={3.0}
          step={0.05}
          onChange={(v) => setControl('pixelErrorThreshold', v)}
          format={(v) => v.toFixed(2)}
        />
        <Slider
          label="GPU budget"
          value={controls.gpuMemoryBudgetMb}
          min={64}
          max={2048}
          step={64}
          onChange={(v) => setControl('gpuMemoryBudgetMb', v)}
          format={(v) => `${v} MB`}
        />
      </section>

      <section className="controls__section">
        <h2>Scene</h2>
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
      </section>
    </div>
  );
}
