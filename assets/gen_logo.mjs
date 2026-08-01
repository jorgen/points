// Generates the dewfall logo: a dewdrop rendered as a point cloud (stippled dots), plus a banner
// with the wordmark. Deterministic (seeded LCG) so regeneration is reproducible.
// Usage: node gen_logo.mjs <outdir>

const outdir = process.argv[2] ?? '.';
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

// ---- deterministic rng ----
let seed = 424242;
const rand = () => {
  seed = (seed * 1103515245 + 12345) & 0x7fffffff;
  return seed / 0x7fffffff;
};

// ---- droplet geometry (unit space: x in [-0.5, 0.5], y in [0, 1], apex at top y=0) ----
// Classic teardrop: circle of radius R centered at (0, cy) + tangent cone to the apex (0, 0).
const R = 0.30;
const CY = 0.62;
function insideDrop(x, y) {
  const dx = x, dy = y - CY;
  if (dx * dx + dy * dy <= R * R) return true; // body circle
  // cone from apex tangent to the circle: at height y (0..CY), allowed |x| grows linearly to the tangent width
  if (y < 0.09 || y > CY) return false;
  const d = Math.hypot(0 - 0, CY - 0);           // apex to center distance
  const tanHalf = R / Math.sqrt(d * d - R * R);  // tangent half-angle
  return Math.abs(x) <= y * tanHalf;
}
// approximate distance to the boundary (for edge falloff)
function edgeDist(x, y) {
  const dCircle = R - Math.hypot(x, y - CY);
  const d = Math.hypot(0, CY);
  const tanHalf = R / Math.sqrt(d * d - R * R);
  const dCone = (y * tanHalf - Math.abs(x)) * Math.cos(Math.atan(tanHalf));
  return Math.max(dCircle, Math.min(dCone, y));
}

// ---- gradient: light sky-cyan at top -> deep blue at bottom ----
function lerp(a, b, t) { return a + (b - a) * t; }
function hex(r, g, b) {
  return '#' + [r, g, b].map((v) => Math.round(Math.max(0, Math.min(255, v))).toString(16).padStart(2, '0')).join('');
}
function dotColor(y, jitter) {
  const t = Math.min(1, Math.max(0, y * 1.15 - 0.05 + jitter * 0.08));
  // #7dd3fc (125,211,252) -> #0c4a6e (12,74,110) via #0284c7
  const mid = 0.55;
  if (t < mid) {
    const u = t / mid;
    return hex(lerp(125, 2, u), lerp(211, 132, u), lerp(252, 199, u));
  }
  const u = (t - mid) / (1 - mid);
  return hex(lerp(2, 12, u), lerp(132, 74, u), lerp(199, 110, u));
}

// ---- stipple the drop ----
function makeDots() {
  const dots = [];
  const N = 27; // grid resolution
  for (let gy = 0; gy <= N * 1.0; gy++) {
    for (let gx = -N / 2; gx <= N / 2; gx++) {
      const x = gx / N + (rand() - 0.5) * (0.55 / N);
      const y = gy / N + (rand() - 0.5) * (0.55 / N);
      if (!insideDrop(x, y)) continue;
      // glossy highlight void: skip a small ellipse upper-left of the body
      const hx = (x + 0.115) / 0.062, hy = (y - 0.50) / 0.10;
      if (hx * hx + hy * hy < 1) continue;
      const e = edgeDist(x, y);
      const rBase = 0.008 + Math.min(0.024, e * 0.26);
      const r = rBase * (0.75 + rand() * 0.5);
      dots.push({ x, y, r, c: dotColor(y, rand()), o: 0.85 + rand() * 0.15 });
    }
  }
  return dots;
}

function dropSvg(dots, size, pad) {
  const s = size - 2 * pad;
  const cx = size / 2;
  const parts = dots.map((d) =>
    `<circle cx="${(cx + d.x * s).toFixed(2)}" cy="${(pad + d.y * s).toFixed(2)}" r="${(d.r * s).toFixed(2)}" fill="${d.c}" fill-opacity="${d.o.toFixed(2)}"/>`);
  return parts.join('\n    ');
}

const dots = makeDots();

// ---- icon (square) ----
const ICON = 512, PAD = 48;
const icon = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${ICON} ${ICON}" width="${ICON}" height="${ICON}">
  <title>dewfall</title>
  <g>
    ${dropSvg(dots, ICON, PAD)}
  </g>
</svg>
`;
writeFileSync(join(outdir, 'logo.svg'), icon);

// ---- banner (wordmark + tagline) ----
const BH = 260, BW = 900, DS = 240, DPAD = 14;
const banner = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${BW} ${BH}" width="${BW}" height="${BH}">
  <title>dewfall — point clouds, settled</title>
  <g transform="translate(30, 10)">
    ${dropSvg(dots, DS, DPAD)}
  </g>
  <g font-family="ui-sans-serif, system-ui, -apple-system, 'Segoe UI', sans-serif">
    <text x="300" y="150" font-size="104" font-weight="650" letter-spacing="-2" fill="#0c4a6e">dew<tspan fill="#0284c7">fall</tspan></text>
    <text x="304" y="200" font-size="30" fill="#64748b">point clouds, settled.</text>
  </g>
</svg>
`;
writeFileSync(join(outdir, 'banner.svg'), banner);
console.log(`wrote logo.svg (${dots.length} dots) and banner.svg to ${outdir}`);
