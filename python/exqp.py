#! /usr/bin/env python3
"""QP-table encoding example for cv2.vcucodec.

Decodes an AVC/HEVC input, re-encodes it while applying a raw per-LCU QP table,
and writes the resulting bitstream. Each --test selects a different QP-table
scenario so the feature can be exercised without editing the script.

The QP table is a flat CV_8U/NumPy buffer of exactly ``enc.qpTableBufferSize()``
bytes, laid out one fixed-size record per LCU (see the "QP table format" page).
Byte 0 of each record is the CTB-level QP / relative dQP; the sub-block bytes are
left 0 so every coding unit inherits the CTB value.

Examples:
  ./exqp.py --avc -i in.avc -o out.avc                 # test 0: no QP table (baseline)
  ./exqp.py --avc -i in.avc -o out.avc --test 1        # uniform dQP = -6
  ./exqp.py --avc -i in.avc -o out.avc --test 2        # centre -10 / border +10
  ./exqp.py --avc -i in.avc -o out.avc --test 3        # dQP applied from frame 30
  ./exqp.py --avc -i in.avc -o out.avc --test 4        # absolute QP = 40 (ABSOLUTE mode)
"""
import argparse
import cv2
import cv2.vcucodec as vcu
import numpy as np
from formats import FOURCC


def mode_for_test(test):
    """The create-time QP-table mode a test needs (must match setQpTable's mode)."""
    return vcu.QpTableMode_ABSOLUTE if test == 4 else vcu.QpTableMode_RELATIVE


def build_qp(enc, test, mode):
    """Build and schedule the QP table(s) for the selected test.

    Returns a human-readable description. Byte 0 of every per-LCU record carries the
    CTB QP (absolute) or relative dQP; the remaining record bytes stay 0.
    """
    cols, rows = enc.qpTableGridSize()
    stride = enc.qpTableBytesPerLCU()
    nlcu = cols * rows
    base = np.arange(nlcu) * stride           # byte-0 offset of each LCU record

    def new_buf():
        return np.zeros(enc.qpTableBufferSize(), np.uint8)

    if test == 0:
        return "no QP table (baseline)"

    if test == 1:
        buf = new_buf()
        buf[base] = np.uint8(-6 & 0xFF)       # -6 relative on every LCU
        enc.setQpTable(0, buf, mode)
        return "uniform relative dQP = -6 on all LCUs"

    if test == 2:
        buf = new_buf()
        lx = np.arange(nlcu) % cols
        ly = np.arange(nlcu) // cols
        centre = ((lx >= cols // 3) & (lx < 2 * cols // 3) &
                  (ly >= rows // 3) & (ly < 2 * rows // 3))
        buf[base] = np.where(centre, -10 & 0xFF, 10 & 0xFF).astype(np.uint8)
        enc.setQpTable(0, buf, mode)
        return "centre CTBs dQP = -10, border CTBs dQP = +10"

    if test == 3:
        buf = new_buf()
        buf[base] = np.uint8(-8 & 0xFF)
        enc.setQpTable(30, buf, mode)         # neutral until frame 30, then applied
        return "relative dQP = -8 applied from frame 30 onward"

    if test == 4:
        buf = new_buf()
        buf[base] = np.uint8(40)              # absolute QP 40 on every LCU
        enc.setQpTable(0, buf, mode)
        return "absolute QP = 40 on all LCUs (ABSOLUTE mode)"

    raise SystemExit(f"Unknown --test {test} (valid: 0, 1, 2, 3, 4)")


def main():
    parser = argparse.ArgumentParser(description="QP-table encoding example")
    codec_group = parser.add_mutually_exclusive_group(required=True)
    codec_group.add_argument("--avc", "-avc", action="store_true", help="Use AVC codec")
    codec_group.add_argument("--hevc", "-hevc", action="store_true", help="Use HEVC codec")
    parser.add_argument("--input", "-i", required=True, help="Input bitstream path")
    parser.add_argument("--output", "-o", required=True, help="Output bitstream path")
    parser.add_argument("--test", "-t", type=int, default=0, help="QP scenario to run (default 0)")
    parser.add_argument("--width", type=int, default=1280, help="Frame width in pixels (default 1280)")
    parser.add_argument("--height", type=int, default=720, help="Frame height in pixels (default 720)")
    parser.add_argument("--max-frames", type=int, default=0, help="Max frames to process (0 = all)")
    args = parser.parse_args()

    codec = vcu.Codec_AVC if args.avc else vcu.Codec_HEVC

    # Decoder: decode the input to raw frames (native format).
    decoderInitParams = vcu.DecoderInitParams(
        codec=vcu.CODEC_AVC if args.avc else vcu.CODEC_HEVC,
        fourcc=FOURCC("NULL"),
        maxFrames=args.max_frames)
    dec = vcu.createDecoder(args.input, decoderInitParams)

    # Encoder: same codec, NV12 at the given resolution. The QP-table mode is fixed
    # at creation and must match the mode passed to setQpTable().
    mode = mode_for_test(args.test)
    params = vcu.EncoderInitParams()
    pic = params.pictureEncSettings
    pic.codec = codec
    pic.fourcc = FOURCC("NV12")
    pic.width = args.width
    pic.height = args.height
    params.pictureEncSettings = pic
    params.qpTableMode = mode
    enc = vcu.createEncoder(args.output, params)

    desc = build_qp(enc, args.test, mode)
    grid = enc.qpTableGridSize()
    print(f"QP test {args.test}: {desc}")
    print(f"  grid={grid} bytesPerLCU={enc.qpTableBytesPerLCU()} "
          f"bufferSize={enc.qpTableBufferSize()}")

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


if __name__ == "__main__":
    main()
