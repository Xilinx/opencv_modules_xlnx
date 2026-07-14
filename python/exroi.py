#! /usr/bin/env python3
"""Region-of-Interest (ROI) encoding example for cv2.vcucodec.

Decodes an AVC/HEVC input, re-encodes it while applying one or more regions of
interest, and writes the resulting bitstream. Each --test selects a different ROI
scenario so the feature can be exercised without editing the script.

Examples:
  ./exroi.py --avc -i in.avc -o out.avc                # test 0: no ROI (baseline)
  ./exroi.py --avc -i in.avc -o out.avc --test 1       # one static HIGH region
  ./exroi.py --avc -i in.avc -o out.avc --test 2       # LOW background + HIGH region
  ./exroi.py --avc -i in.avc -o out.avc --test 3       # region scheduled mid-stream
  ./exroi.py --avc -i in.avc -o out.avc --test 4       # multiple regions, mixed quality
"""
import argparse
import cv2
import cv2.vcucodec as vcu
from formats import FOURCC


def build_test(enc, test, width, height):
    """Create the ROIs for the selected test.

    Returns (rois, description). The handles are returned so the caller keeps them
    alive for the duration of the encode.
    """
    # A centered rectangle covering the middle quarter of the frame.
    cx, cy = width // 4, height // 4
    cw, ch = width // 2, height // 2

    rois = []
    if test == 0:
        # Baseline: no region at all, for comparison against the ROI variants.
        desc = "no ROI (baseline)"

    elif test == 1:
        # Static region: one HIGH-quality rectangle, active for the whole stream.
        roi = enc.createROI((cx, cy, cw, ch), vcu.ROIQuality_HIGH)
        roi.enable(0)
        rois.append(roi)
        desc = f"static HIGH region {(cx, cy, cw, ch)} enabled for the whole stream"

    elif test == 2:
        # Background + foreground: drop the whole picture to LOW quality, then keep
        # the centered rectangle at HIGH quality so it stands out.
        bg = enc.createROIBackground(vcu.ROIQuality_LOW)
        bg.enable(0)
        rois.append(bg)
        fg = enc.createROI((cx, cy, cw, ch), vcu.ROIQuality_HIGH)
        fg.enable(0)
        rois.append(fg)
        desc = f"LOW background + HIGH region {(cx, cy, cw, ch)}"

    elif test == 3:
        # Scheduled region: appears at frame 30 and disappears at frame 60.
        roi = enc.createROI((cx, cy, cw, ch), vcu.ROIQuality_HIGH)
        roi.enable(30)
        roi.disable(60)
        rois.append(roi)
        desc = f"HIGH region {(cx, cy, cw, ch)} active on frames [30, 60)"

    elif test == 4:
        # Multiple regions of different qualities, all active for the whole stream.
        tw, th = width // 3, height // 3
        specs = [
            ((0,            0,            tw, th), vcu.ROIQuality_HIGH,      "top-left HIGH"),
            ((width - tw,   0,            tw, th), vcu.ROIQuality_LOW,       "top-right LOW"),
            ((0,            height - th,  tw, th), vcu.ROIQuality_DONT_CARE, "bottom-left DONT_CARE"),
            ((tw,           th,           tw, th), vcu.ROIQuality_HIGH,      "centre HIGH"),
        ]
        parts = []
        for rect, quality, label in specs:
            roi = enc.createROI(rect, quality)
            roi.enable(0)
            rois.append(roi)
            parts.append(f"{label} {rect}")
        desc = "multiple regions: " + ", ".join(parts)

    else:
        raise SystemExit(f"Unknown --test {test} (valid: 0, 1, 2, 3, 4)")

    return rois, desc


def main():
    parser = argparse.ArgumentParser(description="ROI encoding example")
    codec_group = parser.add_mutually_exclusive_group(required=True)
    codec_group.add_argument("--avc", "-avc", action="store_true", help="Use AVC codec")
    codec_group.add_argument("--hevc", "-hevc", action="store_true", help="Use HEVC codec")
    parser.add_argument("--input", "-i", required=True, help="Input bitstream path")
    parser.add_argument("--output", "-o", required=True, help="Output bitstream path")
    parser.add_argument("--test", "-t", type=int, default=0, help="ROI scenario to run (default 0)")
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

    # Encoder: same codec, NV12 at the given resolution.
    params = vcu.EncoderInitParams()
    pic = params.pictureEncSettings
    pic.codec = codec
    pic.fourcc = FOURCC("NV12")
    pic.width = args.width
    pic.height = args.height
    params.pictureEncSettings = pic
    enc = vcu.createEncoder(args.output, params)

    # Regions must be created before encoding starts.
    rois, desc = build_test(enc, args.test, args.width, args.height)
    print(f"ROI test {args.test}: {desc}")
    for roi in rois:
        print(f"  region={roi.region()} quality={roi.quality()} deltaQP={roi.deltaQP()}")

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
