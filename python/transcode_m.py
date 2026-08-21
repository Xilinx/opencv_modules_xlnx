#! /usr/bin/env python3
"""Multi-channel VCU transcoder.

Runs several transcode channels (decode -> encode) that all read the SAME input
bitstream and share the SAME encoder configuration, each writing a separate
output bitstream (out_0.h265, out_1.h265, ...). Channels run concurrently by
default (one thread each) to exercise multi-channel decode+encode; --sequential
runs them one at a time.

Example:
  ./transcode_m.py --hevc -i in.h265 -o out.h265 -n 4 --cfg cfg/NV12.cfg
  ./transcode_m.py --avc  -i in.h264 -o out.h265 -n 2 --output-format NV12
"""
import cv2
import argparse
import gc
import os
import sys
import threading

from formats import FOURCC, bitdepth_str_to_enum
import vcu_config_parser

vcu = cv2.vcucodec

GREEN = '\033[92m'
BLUE = '\033[94m'
RESET = '\033[0m'


def build_dec_params(args):
    p = vcu.DecoderInitParams(
        codec=vcu.CODEC_AVC if args.avc else vcu.CODEC_HEVC,
        fourcc=FOURCC(args.output_format.upper()),
        maxFrames=args.max_frames,
        bitDepth=bitdepth_str_to_enum(args.bitdepth))
    if args.dmabuf:
        p.extraFrames = 20          # decode queue depth for zero-copy fd transfer
    return p


def build_enc_params(args):
    if args.cfg:
        config = vcu_config_parser.VCUConfigParser()
        config.parse(args.cfg)
        config.validate_keys()
        return config.create_encoder_params()
    return vcu.EncoderInitParams()


def output_name(base, idx, total):
    """out.h265 -> out_0.h265, out_1.h265, ... (single channel keeps the base name)."""
    root, ext = os.path.splitext(base)
    if total == 1:
        return base
    return f"{root}_{idx}{ext}"


def run_channel(idx, input_file, output_file, dec_params, enc_params, use_dmabuf, results):
    """One decode->encode channel. Stores (output, frames, stats/err)."""
    dec = enc = None
    frame = dst = None
    frame_nr = 0
    try:
        dec = vcu.createDecoder(input_file, dec_params)
        enc = vcu.createEncoder(output_file, enc_params)
        while True:
            if use_dmabuf:
                status, fd, info = dec.nextFrameFd()
            else:
                status, frame = dec.nextFrame()

            if status == vcu.DECODE_TIMEOUT:
                continue
            if status == vcu.DECODE_EOS:
                break
            if status == vcu.DECODE_FRAME:
                if use_dmabuf:
                    enc.writeFrameFd(fd)            # zero-copy fd transfer
                else:
                    dst = frame.copyTo()
                    enc.write(dst)
                frame_nr += 1
        enc.eos()
        results[idx] = (output_file, frame_nr, enc.statistics())
    except Exception as e:                          # keep the other channels running
        results[idx] = (output_file, frame_nr, f"ERROR: {e}")
    finally:
        # Release frame references and force GC before destroying the codecs,
        # otherwise AL_Buffer refcount asserts (same as transcode.py).
        frame = dst = None
        gc.collect()
        if enc is not None:
            del enc
        if dec is not None:
            del dec


def main():
    text = "Multi-channel AVC/HEVC transcoder (same input + same cfg, N outputs).\n"
    parser = argparse.ArgumentParser(
        description=text, formatter_class=argparse.RawDescriptionHelpFormatter)
    codec_group = parser.add_mutually_exclusive_group(required=True)
    codec_group.add_argument("--avc", "-avc", action="store_true", help="Input bitstream is AVC/H.264")
    codec_group.add_argument("--hevc", "-hevc", action="store_true", help="Input bitstream is HEVC/H.265")
    parser.add_argument("--input", "-i", required=True, help="Input bitstream (shared by all channels)")
    parser.add_argument("--output", "-o", required=True, help="Output bitstream base name")
    parser.add_argument("--count", "-n", type=int, default=2, help="Number of transcode channels (default 2)")
    parser.add_argument("--cfg", default=None, help="Encoder configuration file (shared by all channels)")
    parser.add_argument("--output-format", type=str, default="NULL", help="Decoder output / encoder input FourCC (e.g. NV12)")
    parser.add_argument("--bitdepth", "-bd", type=str,
                        choices=["8", "10", "12", "alloc", "stream", "first"], default="first",
                        help="Decoded YUV bit depth")
    parser.add_argument("--max-frames", type=int, default=0, help="Max frames to decode per channel (0 = all)")
    parser.add_argument("--dmabuf", "-dmabuf", action="store_true",
                        help="Zero-copy dmabuf fd transfer from decoder to encoder")
    parser.add_argument("--sequential", action="store_true", help="Run channels one at a time instead of concurrently")
    args = parser.parse_args()

    if args.count < 1:
        parser.error("--count must be >= 1")

    dec_params = build_dec_params(args)
    enc_params = build_enc_params(args)          # shared; createEncoder copies it
    outputs = [output_name(args.output, i, args.count) for i in range(args.count)]

    mode = "sequentially" if args.sequential else "concurrently"
    print(f"{GREEN}Multi-channel transcode: {args.count} channels {mode}"
          f"{' (dmabuf)' if args.dmabuf else ''}{RESET}")
    print(f"  input:  {args.input} ({'AVC' if args.avc else 'HEVC'})")
    print(f"  cfg:    {args.cfg or '(defaults)'}")
    for i, out in enumerate(outputs):
        print(f"  [ch {i}] -> {out}")

    results = {}
    if args.sequential:
        for i, out in enumerate(outputs):
            run_channel(i, args.input, out, dec_params, enc_params, args.dmabuf, results)
    else:
        threads = []
        for i, out in enumerate(outputs):
            t = threading.Thread(target=run_channel,
                                 args=(i, args.input, out, dec_params, enc_params, args.dmabuf, results))
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
        size = os.path.getsize(out) if os.path.exists(out) else 0
        print(f"[ch {i}] {out} ({size} bytes, {nframes} frames)")
        print("  " + str(stats).strip().replace("\n", "\n  "))

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
