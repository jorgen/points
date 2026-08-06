#!/usr/bin/env python3
"""Query a box out of a .dew dataset and render the points.

    python examples/python/query_box.py scan.dew --box 0,0,0,50,50,20
    python examples/python/query_box.py scan.dew --box 0,0,0,50,50,20 --color intensity -o box.png

With no dataset argument the script converts a small synthetic cloud first, so it runs standalone::

    python examples/python/query_box.py

Reading is REQUEST-BASED. `query_box` submits a request, waits for it, copies the results into numpy
arrays that Python owns, and releases the request -- so nothing here points into library memory once
the call returns. (The converter's callbacks in numpy_to_dew.py are the opposite: those buffers alias
library memory and are only valid during the call.)

Three parameters are worth understanding, because they change what you get back:

* `clip_points` -- the octree selects whole NODES, so a box query naturally overshoots. True runs a
  per-point test after decoding and returns exactly the points inside the box; False returns the
  overlapping nodes whole, which is faster and fine when you are about to filter anyway.

* `lod` -- 'full' returns every source point in the box. 'level' stops at a coarser octree level and
  'budget' descends while under `max_points`; both return a SUBSAMPLE, which is what you want for a
  quick look at a huge region. They are never mixed: a query returns one level of detail, never a
  node together with its ancestors.

* `position_format` -- 'r64' gives absolute float64 world coordinates and is lossless. 'r32'/'i32'
  are relative to each node's origin and are for feeding a renderer, not for measuring.
"""

import argparse
import sys

import numpy as np

import dew

SYNTHETIC = "query_box_example.dew"


def build_synthetic(path, side=64, spacing=0.25):
    """Convert a synthetic slab, so the example runs with no input file.

    Reuses numpy_to_dew.py's converter callbacks rather than re-deriving them: writing to a dataset
    goes through three callbacks and a quantization step that are worth learning ONCE, in the example
    dedicated to them.
    """
    import os

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from numpy_to_dew import Source, build

    grid = np.stack(
        np.meshgrid(np.arange(side), np.arange(side), np.arange(side // 4), indexing="ij"),
        axis=-1,
    ).reshape(-1, 3).astype(np.float64)
    xyz = grid * spacing

    # Something recognisable to colour by: distance from the slab's vertical axis.
    centre = xyz[:, :2].mean(axis=0)
    radial = np.linalg.norm(xyz[:, :2] - centre, axis=1)
    normalized = radial / max(radial.max(), 1e-9)
    intensity = (normalized * 65535).astype(np.uint16)
    rgb = np.empty((len(xyz), 3), dtype=np.uint16)
    rgb[:, 0] = intensity
    rgb[:, 1] = 65535 - intensity
    rgb[:, 2] = 32768
    classification = np.full(len(xyz), 2, dtype=np.uint8)

    # A stale or half-written file makes the converter refuse to open it, so start clean.
    if os.path.exists(path):
        os.remove(path)

    converter, _ = build(path, Source(xyz, rgb, intensity, classification, scale=spacing / 100.0))
    del converter  # flushed by wait_idle inside build(); drop the handle before reopening to read
    return path


def render(xyz, colour, colour_name, out_path):
    """Scatter-plot the queried points, coloured by the requested attribute."""
    try:
        import matplotlib

        if out_path:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from mpl_toolkits.mplot3d import Axes3D  # noqa: F401  (registers the 3d projection)
    except ImportError:
        print("matplotlib is not installed; skipping the render " "(pip install matplotlib)", file=sys.stderr)
        return

    figure = plt.figure(figsize=(9, 7))
    axes = figure.add_subplot(111, projection="3d")
    # Scale the marker to the point count: a fixed size is invisible for a small query and a solid
    # blob for a large one.
    size = float(np.clip(30000.0 / max(len(xyz), 1), 0.4, 12.0))
    scatter = axes.scatter(
        xyz[:, 0], xyz[:, 1], xyz[:, 2],
        c=colour, s=size, marker=".", cmap="viridis", linewidths=0, depthshade=False,
    )
    if colour is not None:
        figure.colorbar(scatter, ax=axes, shrink=0.6, label=colour_name)
    axes.set_xlabel("x")
    axes.set_ylabel("y")
    axes.set_zlabel("z")
    axes.set_title(f"{len(xyz):,} points")
    # Equal aspect: a point cloud plotted with stretched axes is actively misleading.
    extents = np.array([xyz[:, i].max() - xyz[:, i].min() for i in range(3)])
    centre = np.array([(xyz[:, i].max() + xyz[:, i].min()) / 2 for i in range(3)])
    radius = extents.max() / 2 or 1.0
    axes.set_xlim(centre[0] - radius, centre[0] + radius)
    axes.set_ylim(centre[1] - radius, centre[1] + radius)
    axes.set_zlim(centre[2] - radius, centre[2] + radius)

    if out_path:
        figure.savefig(out_path, dpi=140, bbox_inches="tight")
        print(f"wrote {out_path}")
    else:
        plt.show()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dataset", nargs="?", help="path or URL of a .dew dataset (default: build a synthetic one)")
    parser.add_argument("--box", help="minx,miny,minz,maxx,maxy,maxz (default: the middle of the dataset)")
    parser.add_argument("--color", "--colour", dest="colour", default="intensity", help="attribute to colour by (default: intensity)")
    parser.add_argument("--lod", default="full", choices=["full", "level", "budget"], help="how far to descend the octree")
    parser.add_argument("--level", type=int, default=0, help="octree level for --lod level (larger is coarser)")
    parser.add_argument("--max-points", type=int, default=200_000, help="point budget for --lod budget")
    parser.add_argument("--whole-nodes", action="store_true", help="return overlapping nodes whole instead of clipping to the box")
    parser.add_argument("--connection", default="", help="cloud connection string for s3:// / az:// datasets")
    parser.add_argument("-o", "--output", help="write a PNG instead of opening a window")
    args = parser.parse_args(argv)

    path = args.dataset
    if path is None:
        print(f"no dataset given; converting a synthetic cloud into {SYNTHETIC}")
        path = build_synthetic(SYNTHETIC)

    dataset = dew.open_dataset(path, args.connection)
    info = dataset.get_info()
    names = [dataset.get_attribute_name(i) for i in range(dataset.attribute_count())]
    print(f"dataset {path}")
    print(f"  bounds     {list(info.aabb_min)} .. {list(info.aabb_max)}")
    print(f"  scale      {info.scale}")
    print(f"  attributes {names}")

    if args.box:
        values = [float(v) for v in args.box.split(",")]
        if len(values) != 6:
            parser.error("--box needs six comma-separated numbers")
        box_min, box_max = values[:3], values[3:]
    else:
        # Default to the middle half of the DATA. info.aabb_* is the root octree cell -- a
        # power-of-two cube that is often far larger than the points inside it -- so a box derived
        # from it can easily miss everything. Ask the dataset instead: one cheap coarse query over
        # the whole cell, then use the bounds of what actually comes back.
        probe = dataset.query_box(
            list(info.aabb_min), list(info.aabb_max), lod="budget", max_points=20000, clip_points=False
        )
        if probe["point_count"] == 0:
            print("dataset appears to be empty", file=sys.stderr)
            return 1
        lo, hi = probe["xyz"].min(axis=0), probe["xyz"].max(axis=0)
        centre, half = (lo + hi) / 2, (hi - lo) / 4
        box_min, box_max = list(centre - half), list(centre + half)
    print(f"  querying   {box_min} .. {box_max}")

    wanted = [args.colour] if args.colour in names else []
    result = dataset.query_box(
        box_min,
        box_max,
        attributes=wanted,
        lod=args.lod,
        level=args.level,
        max_points=args.max_points,
        clip_points=not args.whole_nodes,
        position_format="r64",
    )

    xyz = result["xyz"]
    print(f"  got        {result['point_count']:,} points from {result['node_count']} nodes")
    if result["point_count"] == 0:
        print("nothing in that box", file=sys.stderr)
        return 1
    print(f"  actual     {list(xyz.min(axis=0))} .. {list(xyz.max(axis=0))}")

    colour = result.get(args.colour) if wanted else None
    if colour is None:
        colour = xyz[:, 2]  # fall back to height, which always exists
        colour_name = "z"
    else:
        colour_name = args.colour

    render(xyz, colour, colour_name, args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
