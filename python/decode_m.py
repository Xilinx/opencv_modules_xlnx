#! /usr/bin/env python3
"""Multi-stream VCU decoder.

Runs several decoders on the SAME input bitstream, each optionally writing its
own decoded YUV. Because the raw YUV output is huge, output is discarded by
default; pass -o /dev/null (or --no-yuv) to force discard, or -o <file> to write
per-decoder YUV files (out_0.yuv, out_1.yuv, ...). Decoders run concurrently by
default (one thread each) to exercise multi-channel decoding; --sequential runs
them one at a time.

Example:
  ./decode_m.py --hevc -i in.h265 -n 4                 # decode x4, discard output
  ./decode_m.py --hevc -i in.h265 -n 4 -o /dev/null    # same (explicit discard)
  ./decode_m.py --hevc -i in.h265 -n 2 -o out.yuv      # -> out_0.yuv, out_1.yuv
"""
import cv2
import argparse
import gc
import os
import sys
import threading

from formats import FOURCC, write2file, bitdepth_str_to_enum

vcu = cv2.vcucodec

GREEN = '\033[92m'
BLUE = '\033[94m'
RESET = '\033[0m'

# Output paths that mean "don't write the decoded YUV".
DISCARD_PATHS = {"", "/dev/null", "null", "none"}


def output_name(base, idx, total):
    """out.yuv -> out_0.yuv, out_1.yuv, ... (single decoder keeps the base name)."""
    root, ext = os.path.splitext(base)
    if not ext:
        ext = ".yuv"
    if total == 1:
        return root + ext
    return f"{root}_{idx}{ext}"


def make_params(args):
    return vcu.DecoderInitParams(
        codec=vcu.CODEC_AVC if args.avc else vcu.CODEC_HEVC,
        fourcc=FOURCC(args.output_format.upper()),
        maxFrames=args.max_frames,
        bitDepth=bitdepth_str_to_enum(args.bitdepth))


def run_one(idx, input_file, output_file, params, discard, results):
    """Decode input_file with one decoder; optionally write YUV. Stores result."""
    dec = None
    fh = None
    frame_nr = 0
    frame = planes = None
    try:
        dec = vcu.createDecoder(input_file, params)
        if not discard:
            fh = open(output_file, 'wb')
        while True:
            status, frame = dec.nextFrame()
            if status == vcu.DECODE_TIMEOUT:
                continue
            if status == vcu.DECODE_EOS:
                break
            if status == vcu.DECODE_FRAME:
                frame_nr += 1
                if not discard:
                    info = frame.info()
                    planes = frame.copyToVec()
                    write2file(0, fh, None, planes, info)
        results[idx] = (output_file if not discard else "/dev/null", frame_nr, dec.statistics())
    except Exception as e:                    # keep the other streams running
        results[idx] = (output_file, frame_nr, f"ERROR: {e}")
    finally:
        # Release frame references and force GC before destroying the decoder,
        # otherwise AL_Buffer refcount asserts (same as decode.py).
        frame = planes = None
        gc.collect()
        if fh:
            fh.close()
        if dec is not None:
            del dec


def main():
    text = "Multi-stream AVC/HEVC decoder (same input bitstream, N decoders).\n"
    parser = argparse.ArgumentParser(
        description=text, formatter_class=argparse.RawDescriptionHelpFormatter)
    codec_group = parser.add_mutually_exclusive_group(required=True)
    codec_group.add_argument("--avc", "-avc", action="store_true", help="Use AVC/H.264 codec")
    codec_group.add_argument("--hevc", "-hevc", action="store_true", help="Use HEVC/H.265 codec")
    parser.add_argument("--input", "-i", required=True, help="Input bitstream (shared by all decoders)")
    parser.add_argument("--output", "-o", default="",
                        help="Output YUV base name. Omit, or use /dev/null, to discard (default)")
    parser.add_argument("--no-yuv", action="store_true", help="Disable YUV output (discard)")
    parser.add_argument("--count", "-n", type=int, default=2, help="Number of decoders (default 2)")
    parser.add_argument("--output-format", type=str, default="NULL", help="Output FourCC (e.g. NV12)")
    parser.add_argument("--bitdepth", "-bd", type=str,
                        choices=["8", "10", "12", "alloc", "stream", "first"], default="first",
                        help="Output YUV bit depth")
    parser.add_argument("--max-frames", type=int, default=0, help="Max frames to decode per stream (0 = all)")
    parser.add_argument("--sequential", action="store_true", help="Run decoders one at a time instead of concurrently")
    args = parser.parse_args()

    if args.count < 1:
        parser.error("--count must be >= 1")

    discard = args.no_yuv or (args.output in DISCARD_PATHS)
    params = make_params(args)

    if discard:
        outputs = ["/dev/null"] * args.count
    else:
        outputs = [output_name(args.output, i, args.count) for i in range(args.count)]

    mode = "sequentially" if args.sequential else "concurrently"
    print(f"{GREEN}Multi-stream decode: {args.count} x {'AVC' if args.avc else 'HEVC'} {mode}{RESET}")
    print(f"  input:  {args.input}")
    print(f"  output: {'discarded' if discard else ', '.join(outputs)}")

    results = {}
    if args.sequential:
        for i in range(args.count):
            run_one(i, args.input, outputs[i], params, discard, results)
    else:
        threads = []
        for i in range(args.count):
            t = threading.Thread(target=run_one,
                                 args=(i, args.input, outputs[i], params, discard, results))
            t.start()
            threads.append(t)
        for t in threads:
            t.join()

    print(f"\n{GREEN}Results:{RESET}")
    failed = 0
    for i in range(args.count):
        out, nframes, stats = results.get(i, (outputs[i], 0, "ERROR: no result"))
        if isinstance(stats, str) and stats.startswith("ERROR"):
            failed += 1
        print(f"[dec {i}] {out}: {nframes} frames")
        print("  " + str(stats).strip().replace("\n", "\n  "))

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
