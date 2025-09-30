#! /usr/bin/env python3
import cv2
from time import sleep
import argparse
from formats import FOURCC
from formats import bitdepth_str_to_enum
from formats import members_str

parser = argparse.ArgumentParser(description="Encoder command")
codec_group = parser.add_mutually_exclusive_group(required=True)
codec_group.add_argument("--avc", "-avc", action="store_true", help="Use AVC codec")
codec_group.add_argument("--hevc", "-hevc", action="store_true", help="Use HEVC codec")
parser.add_argument("--input", "-i", required=True, help="Input file path")
parser.add_argument("--output", "-o", required=True, help="Output file path")
parser.add_argument("--output-format", type=str, default="NULL", help="Output format")
parser.add_argument("--max-frames", type=int, default=0, help="Maximum number of frames to decode")
parser.add_argument("--bitdepth","-bd", type=str, choices=["8", "10", "12", "alloc", "stream", "first"], default="first",
    help="Output YUV bitdepth (8, 10, 12, alloc : force prealloc if present, if not fallback to first, stream: use current frame bitdepth, first: always use bitdepth of the first decoded frame)")

args = parser.parse_args()

user_bitdepth = bitdepth_str_to_enum(args.bitdepth)

decoderInitParams = cv2.vcucodec.DecoderInitParams(
    codec=cv2.vcucodec.CODEC_AVC if args.avc else cv2.vcucodec.CODEC_HEVC,
    fourcc=FOURCC(args.output_format.upper()),
    fourccConvert=0, # FOURCC("BGR"),
    maxFrames=args.max_frames,
    bitDepth=user_bitdepth)

dec = cv2.vcucodec.createDecoder(args.input, decoderInitParams)
params = cv2.vcucodec.EncoderInitParams()
enc = cv2.vcucodec.createEncoder(args.output, params)
print(members_str(params))
frame_idx = 1;
while True:
    ret, frame, info = dec.nextFrame()
    if not ret:
        print(f"\ngot no frame: EOS: {info.eos}")
        if info.eos:
            break
    else:
        enc.write(frame)
        print(f"\rEncoded frame {frame_idx}", end='')
        frame_idx += 1

enc.eos()
print(enc.statistics())
print(f'Output written to "{args.output}"')

del dec
del enc

