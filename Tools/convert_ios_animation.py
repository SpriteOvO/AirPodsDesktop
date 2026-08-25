#!/usr/bin/env python3
"""Convert an iOS SharingDeviceAssets animation for AirPodsDesktop.

The source videos use a compressed, premultiplied black background. A simple
chroma key also removes black details on the product, while applying a regular
mask multiplies antialiased pixels twice and creates a dark fringe. This
converter keeps the opaque interior, reconstructs alpha only along the edge,
and reverses the black-matte composition onto white.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


OUTPUT_SIZE = "900x450"
SOURCE_SIZE = "1050x1086"
SOURCE_FPS = 60


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--threshold", type=int, default=16)
    parser.add_argument("--edge-scale", type=float, default=1.4)
    parser.add_argument("--gamma", type=float, default=1.2)
    parser.add_argument("--quality", type=int, default=1)
    return parser.parse_args()


def convert(args: argparse.Namespace) -> None:
    if not args.input.is_file():
        raise FileNotFoundError(args.input)
    if not 0 <= args.threshold <= 255:
        raise ValueError("threshold must be between 0 and 255")
    if args.edge_scale <= 0:
        raise ValueError("edge-scale must be greater than zero")
    if args.gamma <= 0:
        raise ValueError("gamma must be greater than zero")
    if not 1 <= args.quality <= 31:
        raise ValueError("quality must be between 1 and 31")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = args.output.with_name(f"{args.output.stem}.tmp{args.output.suffix}")

    video_filter = (
        "[0:v]split=3[original_source][shape_source][luma_source];"
        "[original_source]format=rgb24[original];"
        "[shape_source]format=gray,"
        f"lut=y='if(gt(val,{args.threshold}),255,0)',"
        "floodfill=x=0:y=0:s0=0:d0=128,"
        "lut=y='if(eq(val,128),0,255)',"
        "split=2[shape][core_source];"
        "[core_source]erosion,erosion[core];"
        "[luma_source]format=gray,"
        f"lut=y='min(255,val*{args.edge_scale})'[edge_luma];"
        "[edge_luma][shape]blend=all_expr='A*B/255'[edge];"
        "[core][edge]blend=all_mode=lighten[alpha];"
        "[alpha]lut=y='negval',format=rgb24[white_matte];"
        "[original][white_matte]blend=all_mode=addition,format=rgb24,"
        "crop=1050:525:0:260,"
        f"scale={OUTPUT_SIZE}:flags=lanczos,"
        f"eq=gamma={args.gamma},"
        "colorspace=all=bt709:range=tv:format=yuv420p[output]"
    )

    command = [
        args.ffmpeg, "-v", "error", "-y", "-i", str(args.input),
        "-filter_complex", video_filter, "-map", "[output]", "-an",
        "-c:v", "mpeg4", "-q:v", str(args.quality), "-r", str(SOURCE_FPS),
        str(temporary_output),
    ]
    try:
        subprocess.run(command, check=True)
        temporary_output.replace(args.output)
    finally:
        if temporary_output.exists():
            temporary_output.unlink()

    print(f"Converted animation to {args.output}")


if __name__ == "__main__":
    try:
        convert(parse_args())
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
