#!/usr/bin/env python3
"""Build a .dew dataset from numpy arrays already in memory.

    python examples/python/numpy_to_dew.py out.dew [--points 2000000]

The converter pulls point data through three callbacks rather than taking a
buffer: it decides the chunk size, hands you its own buffers, and you fill
them. That is what lets it convert clouds far larger than RAM -- and it means
this example works the same whether `points` is a small array, a memory-mapped
file, or a generator reading from a database.

Two things are worth knowing before reading the code:

* Coordinates are stored as SCALED INTEGERS. You choose `header.scale` (the
  quantization step, e.g. 1mm) and `header.offset`, then write
  `(xyz - offset) / scale` as int32. The octree's resolution comes from that
  scale, so it is the one number worth thinking about. Registering xyz as
  anything but (i32, components_3) is refused.

* The callbacks run on the converter's own threads. The bindings take the GIL
  for you, but your callbacks must not depend on being called from the main
  thread, and the numpy buffers they receive alias converter memory that is
  only valid for the duration of the call -- fill them, don't keep them.
"""

import argparse
import sys
import time

import numpy as np

import dew


def make_example_cloud(count, rng):
    """Stand-in for whatever your real source is: a noisy spiral with colour."""
    t = np.linspace(0.0, 60.0, count)
    radius = 5.0 + t * 0.05
    xyz = np.empty((count, 3), dtype=np.float64)
    xyz[:, 0] = radius * np.cos(t) + rng.normal(0.0, 0.05, count)
    xyz[:, 1] = radius * np.sin(t) + rng.normal(0.0, 0.05, count)
    xyz[:, 2] = t * 0.5 + rng.normal(0.0, 0.05, count)

    height = (xyz[:, 2] - xyz[:, 2].min()) / max(np.ptp(xyz[:, 2]), 1e-9)
    rgb = np.empty((count, 3), dtype=np.uint16)
    rgb[:, 0] = (height * 65535).astype(np.uint16)
    rgb[:, 1] = ((1.0 - height) * 65535).astype(np.uint16)
    rgb[:, 2] = 32768
    intensity = (height * 65535).astype(np.uint16)
    classification = np.where(xyz[:, 2] < 10.0, 2, 5).astype(np.uint8)  # ground / vegetation
    return xyz, rgb, intensity, classification


class Source:
    """One input's state: the arrays plus how far we have got through them."""

    def __init__(self, xyz, rgb, intensity, classification, scale):
        self.xyz = xyz
        self.rgb = rgb
        self.intensity = intensity
        self.classification = classification
        self.scale = scale
        self.offset = np.floor(xyz.min(axis=0))  # keep the quantized ints small
        self.produced = 0

    @property
    def count(self):
        return len(self.xyz)


def build(path, source, compression=dew.ConverterCompression.zstd):
    converter = dew.Converter(path, dew.ConverterOpenFileSemantics.truncate)
    converter.set_compression(compression)

    # ---- callback 1: cheap up-front estimate ---------------------------------
    # Used for scheduling and progress only; approximations are fine. Returning
    # None means "nothing known", which is also fine.
    def pre_init(filename):
        info = dew.ConverterFilePreInitInfo()
        info.approximate_point_count = source.count
        info.found_point_count = 1
        info.approximate_point_size_bytes = 20  # i32x3 + u16x3 + u16 + u8
        info.input_file_size_bytes = source.count * 20
        # Telling the converter the source's native precision lets it adopt a
        # matching octree scale instead of a conservative default.
        info.scale = [source.scale] * 3
        info.found_scale = 1
        return info

    # ---- callback 2: declare the schema, return per-file state ---------------
    def init(filename, header, attributes):
        header.point_count = source.count
        header.scale = [source.scale] * 3
        header.offset = list(source.offset)
        # min/max must bracket the data in WORLD coordinates: the converter
        # sizes the octree (and picks the morton width) from them.
        header.min = list(source.xyz.min(axis=0))
        header.max = list(source.xyz.max(axis=0))

        # xyz first, always, as i32x3. The rest is your choice; using the
        # dew.ATTRIBUTE_* names means the renderer and `dew info` recognise them.
        attributes.add_attribute(dew.ATTRIBUTE_XYZ, dew.Type.i32, dew.Components.components_3)
        attributes.add_attribute(dew.ATTRIBUTE_RGB, dew.Type.u16, dew.Components.components_3)
        attributes.add_attribute(dew.ATTRIBUTE_INTENSITY, dew.Type.u16, dew.Components.components_1)
        attributes.add_attribute(dew.ATTRIBUTE_CLASSIFICATION, dew.Type.u8, dew.Components.components_1)
        return source

    # ---- callback 3: fill the converter's buffers ----------------------------
    # `buffers` are writable numpy views onto converter memory, one per
    # attribute in registration order, each shaped (max_points, components).
    # Return how many points you wrote and whether the input is exhausted.
    def convert_data(state, header, attributes, buffers, max_points):
        xyz_out, rgb_out, intensity_out, classification_out = buffers
        begin = state.produced
        end = min(begin + min(max_points, xyz_out.shape[0]), state.count)
        n = end - begin
        if n == 0:
            return 0, True

        # Quantize to the integer grid the dataset is stored on. np.rint before
        # the cast avoids the truncation-toward-zero bias a plain cast gives.
        quantized = np.rint((state.xyz[begin:end] - state.offset) / state.scale)
        xyz_out[:n] = quantized.astype(np.int32)

        rgb_out[:n] = state.rgb[begin:end]
        intensity_out[:n, 0] = state.intensity[begin:end]
        classification_out[:n, 0] = state.classification[begin:end]

        state.produced = end
        return n, state.produced >= state.count

    converter.set_file_converter_callbacks(pre_init=pre_init, init=init, convert_data=convert_data)

    last = [0.0]

    def on_progress(fraction):
        now = time.monotonic()
        if now - last[0] > 0.5:  # callbacks fire often; throttle the printing
            last[0] = now
            print(f"\r  converting {fraction:6.1%}", end="", flush=True)

    warnings = []
    errors = []
    converter.set_runtime_callbacks(
        progress=on_progress,
        warning=warnings.append,
        error=lambda e: errors.append(f"{e.code}: {e.message}"),
    )

    # add_data_file names inputs; with custom callbacks the "filename" is just
    # an identifier your own init/convert_data interpret however they like.
    converter.add_data_file(["numpy://spiral"])
    converter.wait_idle()
    print("\r  converting 100.0%")

    if errors:
        raise RuntimeError("conversion failed: " + "; ".join(errors))
    for warning in warnings:
        print(f"  warning: {warning}")
    if converter.status() != dew.ConverterConversionStatus.completed:
        raise RuntimeError(f"conversion ended in state {converter.status()}")

    stats = converter.get_compression_stats()
    return converter, stats


def report(stats):
    print("\nStored attributes:")
    total_raw = total_packed = 0
    for attribute in stats.attributes:
        ratio = attribute.uncompressed_bytes / max(attribute.compressed_bytes, 1)
        total_raw += attribute.uncompressed_bytes
        total_packed += attribute.compressed_bytes
        print(
            f"  {attribute.name:<16} {attribute.uncompressed_bytes / 1e6:8.2f} MB"
            f" -> {attribute.compressed_bytes / 1e6:7.2f} MB  ({ratio:5.1f}x)"
        )
    print(f"  {'total':<16} {total_raw / 1e6:8.2f} MB -> {total_packed / 1e6:7.2f} MB")


def verify(path):
    """Reopen the finished dataset through the read path."""
    import threading

    renderer = dew.Renderer()
    source = dew.ConverterDataSource(path, renderer)
    names = [source.get_attribute_name(i) for i in range(source.attribute_count())]
    print(f"\nReopened {path}: attributes {names}")

    done = threading.Event()
    box = {}

    def on_aabb(lo, hi):  # fires on a converter thread
        box["min"], box["max"] = list(lo), list(hi)
        done.set()

    source.request_aabb(on_aabb)
    if done.wait(timeout=30.0):
        lo, hi = box["min"], box["max"]
        # Note this is an APPROXIMATION, not the exact bounding box: it decodes
        # the dataset's morton (Z-order) extremes, which are two real points
        # near -- but generally not at -- the corners. It is what the renderer
        # frames the camera with; don't use it as a data integrity check.
        print(f"  approximate extent min ({lo[0]:.2f}, {lo[1]:.2f}, {lo[2]:.2f})")
        print(f"                     max ({hi[0]:.2f}, {hi[1]:.2f}, {hi[2]:.2f})")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", help="path to write (.dew)")
    parser.add_argument("--points", type=int, default=2_000_000, help="how many points to synthesize")
    parser.add_argument("--scale", type=float, default=0.001, help="quantization step in source units")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--no-verify", action="store_true")
    args = parser.parse_args()

    rng = np.random.default_rng(args.seed)
    print(f"Synthesizing {args.points:,} points...")
    cloud = make_example_cloud(args.points, rng)
    source = Source(*cloud, scale=args.scale)

    print(f"Converting to {args.output} (scale {args.scale})...")
    started = time.monotonic()
    converter, stats = build(args.output, source)
    elapsed = time.monotonic() - started
    print(f"  {source.produced:,} points in {elapsed:.1f}s ({source.produced / max(elapsed, 1e-9):,.0f} pts/s)")
    report(stats)

    # Release the converter before reopening: dropping it drains the pipeline.
    del converter

    if not args.no_verify:
        verify(args.output)
    print("\nInspect it further with:  dew info", args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
