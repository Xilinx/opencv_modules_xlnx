#! /usr/bin/env python3
import cv2
import cv2.vcucodec as vcu
import sys
import argparse
from formats import *
import vcu_config_parser

GREEN = '\033[92m'
BLUE = '\033[94m'
YELLOW = '\033[93m'
RESET = '\033[0m'


def show_params(encoder_params, title="VCU Encoder Parameters"):
    """Display encoder parameter values."""
    pic = encoder_params.pictureEncSettings
    rc = encoder_params.rcSettings
    gop = encoder_params.gopSettings
    profile = encoder_params.profileSettings

    # Helper to convert enum values to readable strings
    def codec_str(c):
        return {vcu.Codec_AVC: 'AVC', vcu.Codec_HEVC: 'HEVC', vcu.Codec_JPEG: 'JPEG'}.get(c, str(c))

    def rcmode_str(m):
        return {vcu.RCMode_CONST_QP: 'CONST_QP', vcu.RCMode_CBR: 'CBR', vcu.RCMode_VBR: 'VBR',
                vcu.RCMode_LOW_LATENCY: 'LOW_LATENCY', vcu.RCMode_CAPPED_VBR: 'CAPPED_VBR'}.get(m, str(m))

    def entropy_str(e):
        return {vcu.Entropy_CAVLC: 'CAVLC', vcu.Entropy_CABAC: 'CABAC'}.get(e, str(e))

    def gopmode_str(m):
        return {vcu.GOPMode_BASIC: 'BASIC', vcu.GOPMode_BASIC_B: 'BASIC_B',
                vcu.GOPMode_PYRAMIDAL: 'PYRAMIDAL', vcu.GOPMode_PYRAMIDAL_B: 'PYRAMIDAL_B',
                vcu.GOPMode_LOW_DELAY_P: 'LOW_DELAY_P', vcu.GOPMode_LOW_DELAY_B: 'LOW_DELAY_B',
                vcu.GOPMode_ADAPTIVE: 'ADAPTIVE'}.get(m, str(m))

    def gdrmode_str(m):
        return {vcu.GDRMode_DISABLE: 'DISABLE', vcu.GDRMode_VERTICAL: 'VERTICAL',
                vcu.GDRMode_HORIZONTAL: 'HORIZONTAL'}.get(m, str(m))

    def tier_str(t):
        return {vcu.Tier_MAIN: 'MAIN', vcu.Tier_HIGH: 'HIGH'}.get(t, str(t))

    print(f"\n{GREEN}{title}{RESET}\n")

    print(f"{YELLOW}[PICTURE]{RESET}")
    print(f"  codec      = {codec_str(pic.codec)}")
    print(f"  fourcc     = {fourcc_to_string(pic.fourcc)}")
    print(f"  width      = {pic.width}")
    print(f"  height     = {pic.height}")
    print(f"  framerate  = {pic.framerate}")

    print(f"\n{YELLOW}[RATE_CONTROL]{RESET}")
    print(f"  mode             = {rcmode_str(rc.mode)}")
    print(f"  entropy          = {entropy_str(rc.entropy)}")
    print(f"  bitrate          = {rc.bitrate} kbps")
    print(f"  maxBitrate       = {rc.maxBitrate} kbps")
    print(f"  cpbSize          = {rc.cpbSize} ms")
    print(f"  initialDelay     = {rc.initialDelay} ms")
    print(f"  fillerData       = {rc.fillerData}")
    print(f"  maxQualityTarget = {rc.maxQualityTarget}")
    print(f"  maxPictureSizeI  = {rc.maxPictureSizeI} (0=unlimited)")
    print(f"  maxPictureSizeP  = {rc.maxPictureSizeP} (0=unlimited)")
    print(f"  maxPictureSizeB  = {rc.maxPictureSizeB} (0=unlimited)")
    print(f"  skipFrame        = {rc.skipFrame}")
    print(f"  maxSkip          = {rc.maxSkip} (-1=unlimited)")

    print(f"\n{YELLOW}[GOP]{RESET}")
    print(f"  mode        = {gopmode_str(gop.mode)}")
    print(f"  gdrMode     = {gdrmode_str(gop.gdrMode)}")
    print(f"  gopLength   = {gop.gopLength}")
    print(f"  nrBFrames   = {gop.nrBFrames}")
    print(f"  longTermRef = {gop.longTermRef}")
    print(f"  longTermFreq= {gop.longTermFreq}")
    print(f"  periodIDR   = {gop.periodIDR}")

    print(f"\n{YELLOW}[PROFILE]{RESET}")
    print(f"  profile = '{profile.profile}' (empty=auto-detect)")
    print(f"  level   = '{profile.level}' (empty=library default)")
    print(f"  tier    = {tier_str(profile.tier)}")
    print()


def show_defaults():
    """Display all default encoder parameter values."""
    params = vcu.EncoderInitParams()
    show_params(params, "VCU Encoder Default Parameters")


def parse_max_picture(value):
    """Parse MaxPicture value: 'ALL' or integer (0 means all frames)."""
    if value is None:
        return 0
    if isinstance(value, str):
        if value.upper() == 'ALL':
            return 0
        return int(value)
    return int(value)


def main():
    text = "AVC/HEVC Encoder\n"
    text += "\nEncode raw YUV files to AVC/HEVC bitstreams using VCU hardware.\n"
    text += "\nExample usage:\n"
    text += "  ./encode.py --hevc --cfg enc.cfg --input video.yuv --output video.hevc\n"
    text += "  ./encode.py --avc --cfg enc.cfg --input video.yuv --output video.264\n"
    text += "  ./encode.py --hevc --cfg enc.cfg --first-picture 10 --max-picture 100\n"
    text += "  ./encode.py --show                # show defaults only\n"
    text += "  ./encode.py --show --cfg enc.cfg  # show defaults and config values\n"
    text += "\n"

    parser = argparse.ArgumentParser(description=text, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--show", action="store_true", help="Show encoder parameters (defaults, or with --cfg also config values)")
    codec_group = parser.add_mutually_exclusive_group()
    codec_group.add_argument("--avc", action="store_true", help="Encode using AVC/H.264 codec")
    codec_group.add_argument("--hevc", action="store_true", help="Encode using HEVC/H.265 codec (default)")
    parser.add_argument("--cfg", "-c", help="Input configuration file path")
    parser.add_argument("--input", "-i", help="Input YUV file (overrides config YUVFile)")
    parser.add_argument("--output", "-o", help="Output bitstream file (overrides config BitstreamFile)")
    parser.add_argument("--first-picture", "-f", type=int, help="First picture index (overrides config FirstPicture)")
    parser.add_argument("--max-picture", "-m", help="Max pictures to encode, or 'ALL' (overrides config MaxPicture)")
    parser.add_argument("--quiet", "-q", action="store_true", help="Suppress output messages")

    args = parser.parse_args()

    # Handle --show option without config (show defaults only)
    if args.show and not args.cfg:
        show_defaults()
        sys.exit(0)

    # Config file is required for encoding (unless just showing defaults)
    if not args.cfg:
        parser.error("--cfg is required for encoding (use --show to see default values)")

    # Parse configuration file
    config = vcu_config_parser.VCUConfigParser()
    config.parse(args.cfg)

    # Warn about unknown/deprecated config keys
    config.validate_keys()

    encoder_params = config.create_encoder_params()

    # Set codec from command line (default to HEVC)
    # Note: Need to get/modify/reassign because Python bindings return copies
    pic = encoder_params.pictureEncSettings
    if args.avc:
        pic.codec = vcu.Codec_AVC
    else:
        pic.codec = vcu.Codec_HEVC
    encoder_params.pictureEncSettings = pic

    # Get input file from command line or config
    input_file = args.input
    if not input_file:
        if 'INPUT' in config.sections and 'yuvfile' in config.sections['INPUT']:
            input_file = config.sections['INPUT']['yuvfile']
        else:
            print(f"{BLUE}Error: No input file specified. Use --input or set YUVFile in config.{RESET}")
            sys.exit(1)

    # Get output file from command line or config
    output_file = args.output
    if not output_file:
        if 'OUTPUT' in config.sections and 'bitstreamfile' in config.sections['OUTPUT']:
            output_file = config.sections['OUTPUT']['bitstreamfile']
        else:
            print(f"{BLUE}Error: No output file specified. Use --output or set BitstreamFile in config.{RESET}")
            sys.exit(1)

    # Get first picture from command line or config [RUN] section
    first_picture = args.first_picture
    if first_picture is None:
        if 'RUN' in config.sections and 'firstpicture' in config.sections['RUN']:
            first_picture = int(config.sections['RUN']['firstpicture'])
        else:
            first_picture = 0

    # Get max picture from command line or config [RUN] section
    max_picture_value = args.max_picture
    if max_picture_value is None:
        if 'RUN' in config.sections and 'maxpicture' in config.sections['RUN']:
            max_picture_value = config.sections['RUN']['maxpicture']
    max_picture = parse_max_picture(max_picture_value)

    # Print encoding parameters
    pic = encoder_params.pictureEncSettings
    rc = encoder_params.rcSettings
    gop = encoder_params.gopSettings

    if not args.quiet or args.show:
        if args.show:
            show_params(encoder_params, "VCU Encoder Config Parameters")
        print(f"\n{GREEN}VCU Encoder{RESET}")
        print(f"  Input:   {input_file}")
        print(f"  Output:  {output_file}")
        print(f"  Size:    {pic.width}x{pic.height}")
        print(f"  Codec:   {'HEVC' if pic.codec == vcu.Codec_HEVC else 'AVC'}")
        print(f"  Bitrate: {rc.bitrate} kbps")
        print(f"  GOP:     {gop.gopLength}")
        print(f"  Range:   first={first_picture}, max={max_picture if max_picture > 0 else 'ALL'}")
        print()

    # Create encoder
    try:
        encoder = vcu.createEncoder(output_file, encoder_params)
    except Exception as e:
        print(f"{BLUE}Error creating encoder: {e}{RESET}")
        sys.exit(1)

    # Encode the file
    if not args.quiet:
        print(f"Encoding {input_file}...")
    try:
        encoder.writeFile(input_file, first_picture, max_picture)
    except Exception as e:
        print(f"{BLUE}Error during encoding: {e}{RESET}")
        sys.exit(1)

    # Signal end of stream and wait for completion
    encoder.eos()

    if not args.quiet:
        print(f"{GREEN}Encoding complete: {output_file}{RESET}")


if __name__ == "__main__":
    main()


