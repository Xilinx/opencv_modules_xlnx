#! /usr/bin/env python3
import cv2
import sys
import argparse
from formats import *
text = "AVC/HEVC Decoder\n"
text += "\n\nExample usage:\n"
text += "  ./decode.py -avc --input input.h264 --output output.yuv --output-format NV12\n"
text += "\nSupported: " + cv2.vcucodec.Decoder.getFourCCs()
text += "\n\n"
parser = argparse.ArgumentParser(description=text, formatter_class=argparse.RawDescriptionHelpFormatter)
codec_group = parser.add_mutually_exclusive_group(required=True)
codec_group.add_argument("--avc", "-avc", action="store_true", help="Use AVC codec")
codec_group.add_argument("--hevc", "-hevc", action="store_true", help="Use HEVC codec")
parser.add_argument("--input", "-i", required=True, help="Input file path")
parser.add_argument("--output", "-o", required=True, help="Output file path")
parser.add_argument("--output-format", type=str, default="NULL", help="Output format")
parser.add_argument("--convert", type=str, default="NULL", help="Color space conversion")
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
info = cv2.vcucodec.RawInfo()
if (decoderInitParams.fourccConvert == 0):
    filename = args.output if args.output.endswith('.yuv') else args.output + '.yuv'
elif (decoderInitParams.fourccConvert == FOURCC("BGR")):
    filename = args.output if args.output.endswith('.bgr') else args.output + '.bgr'
file = open(filename, 'wb')

frame_nr = 0
once = True
while True:
    ret, frame, info = dec.nextFrame()
    if not ret:
        print(f"\ngot no frame: EOS: {info.eos}")
        if info.eos:
            break
    else:
        frame_nr += 1
        if once :
            print()
            print("Stream Info:\n", dec.streamInfo())
            once = False

        print(f"\rDecoded {frame_nr}", end='')
        #print(members_str(info))
        if (decoderInitParams.fourccConvert == 0):
            write(file, frame, info)
            pass
        elif (decoderInitParams.fourccConvert == FOURCC("BGR")):
            writebgr(file, frame)

print (f'Output written to "{filename}"')
print ("Statistics:\n", dec.statistics())
del dec

