#! /usr/bin/env python3
import cv2
import sys
import argparse
from formats import *

GREEN = '\033[92m'
BLUE = '\033[94m'
RESET = '\033[0m'

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
parser.add_argument("--convert", type=str, default="NULL", help="Color space conversion, either BGR or BGRA")
parser.add_argument("--max-frames", type=int, default=0, help="Maximum number of frames to decode")
parser.add_argument("--bitdepth","-bd", type=str, choices=["8", "10", "12", "alloc", "stream", "first"], default="first",
    help="Output YUV bitdepth (8, 10, 12, alloc : force prealloc if present, if not fallback to first, stream: use current frame bitdepth, first: always use bitdepth of the first decoded frame)")
parser.add_argument("--no-yuv", action="store_true", help="Disable YUV output")
planes_group = parser.add_mutually_exclusive_group(required=False)
planes_group.add_argument('--planes', action='store_true', help='Use nextFramePlanes() instead of nextFrame()')
planes_group.add_argument('--planesref', action='store_true', help='Use nextFramePlanesRef() instead of nextFrame()')

args = parser.parse_args()

user_bitdepth = bitdepth_str_to_enum(args.bitdepth)
if args.convert.upper() == "BGR":
    print ("Converting to BGR ")
    convert = FOURCC("BGR ")
elif args.convert.upper() == "BGRA":
    print ("Converting to BGRA ")
    convert = FOURCC("BGRA")
else:
    convert = 0

decoderInitParams = cv2.vcucodec.DecoderInitParams(
    codec=cv2.vcucodec.CODEC_AVC if args.avc else cv2.vcucodec.CODEC_HEVC,
    fourcc=FOURCC(args.output_format.upper()),
    fourccConvert=convert,
    maxFrames=args.max_frames,
    bitDepth=user_bitdepth)

#decoderInitParams.extraFrames = 2

dec = cv2.vcucodec.createDecoder(args.input, decoderInitParams)
info = cv2.vcucodec.RawInfo()
filename = args.output
if (decoderInitParams.fourccConvert == 0):
    filename = args.output if args.output.endswith('.yuv') else args.output + '.yuv'
elif (decoderInitParams.fourccConvert == FOURCC("BGR")):
    filename = args.output if args.output.endswith('.bgr') else args.output + '.bgr'
elif (decoderInitParams.fourccConvert == FOURCC("BGRA")):
    filename = args.output if args.output.endswith('.bgra') else args.output + '.bgra'

file = open(filename, 'wb')

frame_nr = 0
once = True
while True:
    planes = None
    frame = None
    if args.planes:
        ret, planes, info = dec.nextFramePlanes()
    elif args.planesref:
        ret, planes, info, token = dec.nextFramePlanesRef()
    else:
        ret, frame, info = dec.nextFrame()

    if not ret:
        print(f"\ngot no frame: EOS: {info.eos}")
        if info.eos:
            break
    else:
        frame_nr += 1
        if once :
            print()
            print(f"Stream Info:\n{BLUE}{dec.streamInfo()}{RESET}")
            once = False

        print(f"\rDecoded {frame_nr}", end='')
        #print(f"Decoded {frame_nr}")
        #print(members_str(info))
        if not args.no_yuv:
            write2file(convert, file, frame, planes, info)
if not args.no_yuv:
    print (f'Output written to {GREEN}{filename}{RESET}')
print (f"Statistics:\n{BLUE}{dec.statistics()}{RESET}")
del dec

