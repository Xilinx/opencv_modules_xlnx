#! /usr/bin/env python3
import cv2
import sys
import gc
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
parser.add_argument("--output", "-o", required=False, default="", help="Output file path")
parser.add_argument("--output-format", type=str, default="NULL", help="Output format")
parser.add_argument("--convert", type=str, default="NULL", help="Color space conversion, either BGR or BGRA")
parser.add_argument("--max-frames", type=int, default=0, help="Maximum number of frames to decode")
parser.add_argument("--bitdepth","-bd", type=str, choices=["8", "10", "12", "alloc", "stream", "first"], default="first",
    help="Output YUV bitdepth (8, 10, 12, alloc : force prealloc if present, if not fallback to first, stream: use current frame bitdepth, first: always use bitdepth of the first decoded frame)")
fps_group = parser.add_mutually_exclusive_group(required=False)
fps_group.add_argument("--fps", type=str, default="", help="Frame rate as integer or ratio (e.g., 60 or 60000/1001)")
fps_group.add_argument("--fps-force", type=str, default="", metavar="FPS", help="Force frame rate, overriding stream timing info")
parser.add_argument("--no-yuv", action="store_true", help="Disable YUV output")
parser.add_argument("--zero-copy", action="store_true", help="Use zero-copy numpy access (no data copy from HW buffer)")
parser.add_argument("--extra-frames", type=int, default=0, help="Extra frame buffers for the decode queue (default: 0)")

args = parser.parse_args()

if not args.no_yuv and not args.output:
    parser.error("--output/-o is required unless --no-yuv is specified")

user_bitdepth = bitdepth_str_to_enum(args.bitdepth)

# Get defaults from DecoderInitParams
defaults = cv2.vcucodec.DecoderInitParams()
fps_num = defaults.fpsNum
fps_den = defaults.fpsDen
force_fps = False

# Parse fps: "60", "60/1", or "60000/1001"
fps_str = args.fps_force if args.fps_force else args.fps
if fps_str:
    force_fps = bool(args.fps_force)
    if '/' in fps_str:
        parts = fps_str.split('/')
        fps_num = int(parts[0])
        fps_den = int(parts[1])
        # If ratio like 60/1, convert to 60000/1000
        if fps_den <= 10:
            fps_num *= 1000
            fps_den *= 1000
    else:
        fps_num = int(fps_str) * 1000
        fps_den = 1000

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
    maxFrames=args.max_frames,
    bitDepth=user_bitdepth,
    fpsNum=fps_num,
    fpsDen=fps_den,
    forceFps=force_fps)

if args.extra_frames > 0:
    decoderInitParams.extraFrames = args.extra_frames

dec = cv2.vcucodec.createDecoder(args.input, decoderInitParams)
file = None
filename = args.output
if not args.no_yuv:
    if convert == FOURCC("BGR "):
        filename = args.output if args.output.endswith('.bgr') else args.output + '.bgr'
    elif convert == FOURCC("BGRA"):
        filename = args.output if args.output.endswith('.bgra') else args.output + '.bgra'
    else:
        filename = args.output if args.output.endswith('.yuv') else args.output + '.yuv'
    file = open(filename, 'wb')

frame_nr = 0
once = True
once_warn_zc = True
nPlanes = 0
frame = planeY = planeUV = planeV = planes = dst = None
try:
  while True:
    status, frame = dec.nextFrame()

    if status == cv2.vcucodec.DECODE_TIMEOUT:
        continue
    elif status == cv2.vcucodec.DECODE_EOS:
        print(f"\nEnd of stream")
        break
    elif status == cv2.vcucodec.DECODE_FRAME:
        frame_nr += 1
        if once :
            print()
            print(f"Stream Info:\n{BLUE}{dec.streamInfo()}{RESET}")
            nPlanes = numPlanes(frame.info().fourcc)
            once = False

        print(f"\rDecoded {frame_nr}", end='')
        info = frame.info()
        if convert != 0:
            # BGR/BGRA conversion (always deep-copy)
            if once_warn_zc and args.zero_copy:
                print(f"\nWarning: --zero-copy ignored when --convert is used (convertTo always copies)")
                once_warn_zc = False
            dst = frame.convertTo(convert)
            if not args.no_yuv:
                writebgr(file, dst)
        elif args.zero_copy:
            # Zero-copy: get numpy views directly into HW buffer
            planeY = cv2.vcucodec.plane_numpy(frame, 0)
            if not args.no_yuv:
                writeplane(file, planeY, info.width, info.height, info.stride)
            if nPlanes > 1:
                planeUV = cv2.vcucodec.plane_numpy(frame, 1)
                if not args.no_yuv:
                    w_uv = (info.width + 1) // 2
                    h_uv = planeUV.shape[0]
                    writeplane(file, planeUV, w_uv, h_uv, info.strideChroma)
            if nPlanes > 2:
                planeV = cv2.vcucodec.plane_numpy(frame, 2)
                if not args.no_yuv:
                    w_uv = (info.width + 1) // 2
                    h_uv = planeV.shape[0]
                    writeplane(file, planeV, w_uv, h_uv, info.strideChroma)
        else:
            # Deep-copy plane access
            planes = frame.copyToVec()
            if not args.no_yuv:
                write2file(convert, file, None, planes, info)
except Exception as e:
    print(f"\nError: {e}")
finally:
    # Release all frame references and force garbage collection before
    # destroying the decoder to avoid AL_Buffer refcount assertions.
    frame = planeY = planeUV = planeV = planes = dst = None
    gc.collect()
if not args.no_yuv:
    print(f'Output written to {GREEN}{filename}{RESET}')
    if file:
        file.close()
print(f"Statistics:\n{BLUE}{dec.statistics()}{RESET}")
del dec

