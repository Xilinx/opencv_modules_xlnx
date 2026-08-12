#! /usr/bin/env python3
"""Scaling-list (quantization matrix) encoding example for cv2.vcucodec.

Decodes an AVC/HEVC input, re-encodes it with a selected scaling-list mode, and
writes the resulting bitstream. The scaling list is fixed at encoder creation via
``EncoderInitParams.scalingList`` (see the "Scaling list" page).

Because the effect is on the quantization matrices, run with a constant QP so rate
control does not re-absorb the difference, and use a transform codec that carries
scaling matrices (HEVC main, or AVC high). Compare the output sizes across tests.

Examples:
  ./exscl.py --hevc -i in.h265 -o out.h265                # test 0: FLAT (baseline)
  ./exscl.py --hevc -i in.h265 -o out.h265 --test 1       # DEFAULT codec matrices
  ./exscl.py --hevc -i in.h265 -o out.h265 --test 2       # CUSTOM all-16 (== FLAT oracle)
  ./exscl.py --hevc -i in.h265 -o out.h265 --test 3       # CUSTOM steep (suppress high freq)
  ./exscl.py --hevc -i in.h265 -o out.h265 --test 4       # CUSTOM steep + custom DC coeffs
  ./exscl.py --hevc --validate                            # API negative tests (no I/O)
"""
import argparse
import sys
import cv2
import cv2.vcucodec as vcu
import numpy as np
from formats import FOURCC

# Flat scaling-list buffer geometry: [S][M][C] = 4 * 6 * 64 = 1536 bytes.
SCL_S, SCL_M, SCL_C = 4, 6, 64
SCL_BYTES = SCL_S * SCL_M * SCL_C
DC_BYTES = 8


def steep_matrices():
    """A high-frequency-suppressing matrix set: each 64-coef matrix ramps 16..255.

    Low-frequency coefficients (small scan index) keep the fine step 16; high
    frequencies get progressively coarser, softening detail.
    """
    ramp = np.linspace(16, 255, SCL_C).astype(np.uint8)          # per-matrix 64 coefs
    return np.tile(ramp, SCL_S * SCL_M).astype(np.uint8)         # 24 matrices


def scl_for_test(test):
    """Return (ScalingListSettings, description) for the selected test."""
    scl = vcu.ScalingListSettings()

    if test == 0:
        scl.mode = vcu.ScalingListMode_FLAT
        return scl, "FLAT (all coefficients 16)"

    if test == 1:
        scl.mode = vcu.ScalingListMode_DEFAULT
        return scl, "DEFAULT (codec-specification matrices)"

    if test == 2:
        scl.mode = vcu.ScalingListMode_CUSTOM
        scl.matrices = np.full(SCL_BYTES, 16, np.uint8).tobytes()
        return scl, "CUSTOM all-16 (should match FLAT)"

    if test == 3:
        scl.mode = vcu.ScalingListMode_CUSTOM
        scl.matrices = steep_matrices().tobytes()
        return scl, "CUSTOM steep ramp (16..255, suppress high frequencies)"

    if test == 4:
        scl.mode = vcu.ScalingListMode_CUSTOM
        scl.matrices = steep_matrices().tobytes()
        scl.dcCoeff = np.full(DC_BYTES, 64, np.uint8).tobytes()
        return scl, "CUSTOM steep ramp + DC coefficients = 64"

    raise SystemExit(f"Unknown --test {test} (valid: 0, 1, 2, 3, 4)")


def run_validate():
    """Exercise the CUSTOM validation path; expects cv2.error for bad buffers."""
    def make(mode, matrices=None, dcCoeff=None):
        params = vcu.EncoderInitParams()
        pic = params.pictureEncSettings
        pic.codec = vcu.Codec_HEVC
        pic.fourcc = FOURCC("NV12")
        pic.width, pic.height = 1280, 720
        params.pictureEncSettings = pic
        scl = vcu.ScalingListSettings()
        scl.mode = mode
        if matrices is not None:
            scl.matrices = matrices
        if dcCoeff is not None:
            scl.dcCoeff = dcCoeff
        params.scalingList = scl
        # Creation triggers init()/validation.
        vcu.createEncoder("/tmp/scl_validate.h265", params)

    cases = [
        ("CUSTOM, matrices too short",
         lambda: make(vcu.ScalingListMode_CUSTOM, np.zeros(100, np.uint8).tobytes())),
        ("CUSTOM, matrices empty",
         lambda: make(vcu.ScalingListMode_CUSTOM, b"")),
        ("CUSTOM, dcCoeff wrong size",
         lambda: make(vcu.ScalingListMode_CUSTOM,
                      np.full(SCL_BYTES, 16, np.uint8).tobytes(),
                      np.zeros(4, np.uint8).tobytes())),
    ]
    ok = True
    print("Scaling-list validation (expect cv2.error on each):")
    for desc, build in cases:
        try:
            build()
            print(f"  FAIL: {desc} did NOT raise")
            ok = False
        except cv2.error:
            print(f"  PASS: {desc} raised cv2.error")
    print("VALIDATE:", "ALL PASS" if ok else "FAILURES")
    return ok


def main():
    parser = argparse.ArgumentParser(description="Scaling-list encoding example")
    codec_group = parser.add_mutually_exclusive_group(required=True)
    codec_group.add_argument("--avc", "-avc", action="store_true", help="Use AVC codec (high profile)")
    codec_group.add_argument("--hevc", "-hevc", action="store_true", help="Use HEVC codec (recommended)")
    parser.add_argument("--input", "-i", help="Input bitstream path")
    parser.add_argument("--output", "-o", help="Output bitstream path")
    parser.add_argument("--test", "-t", type=int, default=0, help="Scaling-list scenario (default 0)")
    parser.add_argument("--width", type=int, default=1280, help="Frame width in pixels (default 1280)")
    parser.add_argument("--height", type=int, default=720, help="Frame height in pixels (default 720)")
    parser.add_argument("--qp", type=int, default=30, help="Constant QP to isolate the effect (default 30)")
    parser.add_argument("--max-frames", type=int, default=0, help="Max frames to process (0 = all)")
    parser.add_argument("--validate", action="store_true", help="Run API negative tests only (no I/O)")
    args = parser.parse_args()

    if args.validate:
        raise SystemExit(0 if run_validate() else 1)

    if not args.input or not args.output:
        parser.error("--input and --output are required unless --validate is given")

    codec = vcu.Codec_AVC if args.avc else vcu.Codec_HEVC

    # Decoder: decode the input to raw frames (native format).
    decoderInitParams = vcu.DecoderInitParams(
        codec=vcu.CODEC_AVC if args.avc else vcu.CODEC_HEVC,
        fourcc=FOURCC("NULL"),
        maxFrames=args.max_frames)
    dec = vcu.createDecoder(args.input, decoderInitParams)

    # Encoder: same codec, NV12, constant QP so the scaling-list effect is visible.
    scl, desc = scl_for_test(args.test)
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
    params.scalingList = scl
    enc = vcu.createEncoder(args.output, params)

    print(f"Scaling-list test {args.test}: {desc}")

    frame_idx = 0
    while True:
        status, frame = dec.nextFrame()
        if status == vcu.DECODE_TIMEOUT:
            continue
        elif status == vcu.DECODE_EOS:
            print("\nEnd of stream")
            break
        elif status == vcu.DECODE_FRAME:
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
