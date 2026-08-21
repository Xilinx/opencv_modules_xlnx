#! /usr/bin/env python3
"""Multi-stream VCU encoder.

Runs several encoders that share the same configuration file and the same input
YUV file, each writing a separate output bitstream. By default the encoders run
concurrently (one thread each) so it exercises multi-channel encoding; use
--sequential to run them one after another.

Example:
  ./encode_m.py --hevc --cfg cfg/NV12.cfg -i in.yuv -o out.h265 -n 4
  # -> out_0.h265, out_1.h265, out_2.h265, out_3.h265
"""
import cv2
import cv2.vcucodec as vcu
import argparse
import os
import sys
import threading

import vcu_config_parser

GREEN = '\033[92m'
BLUE = '\033[94m'
RESET = '\033[0m'


def parse_max_picture(value):
    """Parse MaxPicture: 'ALL' or integer (0 means all frames)."""
    if value is None:
        return 0
    if isinstance(value, str):
        return 0 if value.upper() == 'ALL' else int(value)
    return int(value)


def build_encoder_params(cfg_path, avc):
    """Build EncoderInitParams from the cfg file and select the codec."""
    config = vcu_config_parser.VCUConfigParser()
    config.parse(cfg_path)
    config.validate_keys()
    params = config.create_encoder_params()

    pic = params.pictureEncSettings          # bindings return copies: get/modify/reassign
    pic.codec = vcu.Codec_AVC if avc else vcu.Codec_HEVC
    params.pictureEncSettings = pic
    return params, config


def output_name(base, idx, total):
    """out.h265 -> out_0.h265, out_1.h265, ... (single encoder keeps the base name)."""
    if total == 1:
        return base
    root, ext = os.path.splitext(base)
    return f"{root}_{idx}{ext}"


def run_one(idx, output_file, params, input_file, first_picture, max_picture, results):
    """Encode input_file -> output_file with one encoder. Stores (path, stats/err)."""
    try:
        encoder = vcu.createEncoder(output_file, params)
        encoder.writeFile(input_file, first_picture, max_picture)
        encoder.eos()
        results[idx] = (output_file, encoder.statistics())
    except Exception as e:                   # keep other streams running
        results[idx] = (output_file, f"ERROR: {e}")


def cfg_get(config, section, key):
    sec = config.sections.get(section)
    return sec.get(key) if sec else None


def main():
    text = "Multi-stream AVC/HEVC encoder (same cfg + same input YUV, N outputs).\n"
    parser = argparse.ArgumentParser(
        description=text, formatter_class=argparse.RawDescriptionHelpFormatter)
    codec_group = parser.add_mutually_exclusive_group()
    codec_group.add_argument("--avc", action="store_true", help="Encode using AVC/H.264")
    codec_group.add_argument("--hevc", action="store_true", help="Encode using HEVC/H.265 (default)")
    parser.add_argument("--cfg", "-c", required=True, help="Encoder configuration file")
    parser.add_argument("--input", "-i", help="Input YUV file (overrides cfg YUVFile)")
    parser.add_argument("--output", "-o", help="Output bitstream base name (overrides cfg BitstreamFile)")
    parser.add_argument("--count", "-n", type=int, default=2, help="Number of parallel encoders (default 2)")
    parser.add_argument("--first-picture", "-f", type=int, help="First picture index (overrides cfg FirstPicture)")
    parser.add_argument("--max-picture", "-m", help="Max pictures to encode, or 'ALL' (overrides cfg MaxPicture)")
    parser.add_argument("--sequential", action="store_true", help="Run encoders one at a time instead of concurrently")
    args = parser.parse_args()

    if args.count < 1:
        parser.error("--count must be >= 1")

    # All encoders share the same parameters (createEncoder copies them).
    params, config = build_encoder_params(args.cfg, args.avc)

    input_file = args.input or cfg_get(config, 'INPUT', 'yuvfile')
    if not input_file:
        print(f"{BLUE}Error: no input file. Use --input or set YUVFile in the cfg.{RESET}")
        sys.exit(1)

    output_base = args.output or cfg_get(config, 'OUTPUT', 'bitstreamfile')
    if not output_base:
        print(f"{BLUE}Error: no output file. Use --output or set BitstreamFile in the cfg.{RESET}")
        sys.exit(1)

    first_picture = args.first_picture
    if first_picture is None:
        fp = cfg_get(config, 'RUN', 'firstpicture')
        first_picture = int(fp) if fp else 0

    max_value = args.max_picture
    if max_value is None:
        max_value = cfg_get(config, 'RUN', 'maxpicture')
    max_picture = parse_max_picture(max_value)

    mode = "sequentially" if args.sequential else "concurrently"
    print(f"{GREEN}Multi-stream encode: {args.count} x {'AVC' if args.avc else 'HEVC'} "
          f"{mode}{RESET}")
    print(f"  cfg:    {args.cfg}")
    print(f"  input:  {input_file}")
    print(f"  frames: first={first_picture}, max={max_picture if max_picture > 0 else 'ALL'}")

    outputs = [output_name(output_base, i, args.count) for i in range(args.count)]
    for i, out in enumerate(outputs):
        print(f"  [enc {i}] -> {out}")

    results = {}
    if args.sequential:
        for i, out in enumerate(outputs):
            run_one(i, out, params, input_file, first_picture, max_picture, results)
    else:
        threads = []
        for i, out in enumerate(outputs):
            t = threading.Thread(target=run_one,
                                 args=(i, out, params, input_file, first_picture, max_picture, results))
            t.start()
            threads.append(t)
        for t in threads:
            t.join()

    print(f"\n{GREEN}Results:{RESET}")
    failed = 0
    for i in range(args.count):
        out, stats = results.get(i, (outputs[i], "ERROR: no result"))
        if stats.startswith("ERROR"):
            failed += 1
        size = os.path.getsize(out) if os.path.exists(out) else 0
        print(f"[enc {i}] {out} ({size} bytes)")
        print("  " + stats.strip().replace("\n", "\n  "))

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
