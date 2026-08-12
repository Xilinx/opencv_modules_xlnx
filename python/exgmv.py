#! /usr/bin/env python3
"""Global-Motion-Vector (GMV) encoding example / validation for cv2.vcucodec.

Decodes an AVC/HEVC input and re-encodes it, optionally applying a per-frame
global motion vector via Encoder.set(GlobalMotionVector(frame, x, y)). Rate
control is forced to CONST_QP so the effect of the GMV on the bitstream is
visible (a GMV steers the motion-estimation search centre).

The point of the validation: with GMV armed, a non-zero GMV must change the
output bitstream; a zero GMV must be identical to the no-GMV baseline (the
library only applies a GMV when it is non-zero).

Examples:
  ./exgmv.py --avc -i in.avc -o out.avc --test 0     # baseline, no GMV
  ./exgmv.py --avc -i in.avc -o out.avc --test 1     # GMV (64, 0) every frame
  ./exgmv.py --avc -i in.avc -o out.avc --test 2     # GMV (0, 0)  (must equal baseline)
"""
import argparse
import sys
import cv2
import cv2.vcucodec as vcu
from formats import FOURCC


def gmv_for_test(test):
    """Return (apply, x, y): whether to set a per-frame GMV and its components."""
    if test == 0:
        return False, 0, 0            # baseline: never call set()
    if test == 1:
        return True, 64, 0            # strong horizontal GMV
    if test == 2:
        return True, 0, 0             # zero GMV: gated off -> must match baseline
    raise SystemExit(f"Unknown --test {test} (valid: 0, 1, 2)")


def main():
    parser = argparse.ArgumentParser(description="GMV encoding example")
    codec_group = parser.add_mutually_exclusive_group(required=True)
    codec_group.add_argument("--avc", "-avc", action="store_true", help="Use AVC codec")
    codec_group.add_argument("--hevc", "-hevc", action="store_true", help="Use HEVC codec")
    parser.add_argument("--input", "-i", required=True, help="Input bitstream path")
    parser.add_argument("--output", "-o", required=True, help="Output bitstream path")
    parser.add_argument("--test", "-t", type=int, default=0, help="GMV scenario (default 0)")
    parser.add_argument("--width", type=int, default=1280, help="Frame width in pixels (default 1280)")
    parser.add_argument("--height", type=int, default=720, help="Frame height in pixels (default 720)")
    parser.add_argument("--max-frames", type=int, default=0, help="Max frames to process (0 = all)")
    args = parser.parse_args()

    codec = vcu.Codec_AVC if args.avc else vcu.Codec_HEVC
    apply_gmv, gx, gy = gmv_for_test(args.test)

    decoderInitParams = vcu.DecoderInitParams(
        codec=vcu.CODEC_AVC if args.avc else vcu.CODEC_HEVC,
        fourcc=FOURCC("NULL"),
        maxFrames=args.max_frames)
    dec = vcu.createDecoder(args.input, decoderInitParams)

    # Encoder: same codec, NV12, CONST_QP so the GMV effect is not re-absorbed by rate control.
    params = vcu.EncoderInitParams()
    pic = params.pictureEncSettings
    pic.codec = codec
    pic.fourcc = FOURCC("NV12")
    pic.width = args.width
    pic.height = args.height
    params.pictureEncSettings = pic
    rc = params.rcSettings
    rc.mode = vcu.RCMode_CONST_QP
    params.rcSettings = rc
    enc = vcu.createEncoder(args.output, params)

    print(f"GMV test {args.test}: " +
          (f"apply GMV=({gx},{gy}) every frame" if apply_gmv else "no GMV (baseline)"))

    frame_idx = 0
    while True:
        status, frame = dec.nextFrame()
        if status == vcu.DECODE_TIMEOUT:
            continue
        elif status == vcu.DECODE_EOS:
            print("\nEnd of stream")
            break
        elif status == vcu.DECODE_FRAME:
            if apply_gmv:
                enc.set(vcu.GlobalMotionVector(frame_idx, gx, gy))
            enc.write(frame.copyTo())
            frame_idx += 1
            print(f"\rEncoded frame {frame_idx}", end='')

    enc.eos()
    print(enc.statistics())
    print(f'Output written to "{args.output}"')

    del dec
    del enc

    if args.max_frames and frame_idx < args.max_frames:
        print(f"Error: input holds {frame_idx} frames, {args.max_frames} requested", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
