#! /usr/bin/env python3
import cv2
import cv2.vcucodec as vcu
import sys
import argparse
from formats import *
import vcu_config_parser

GREEN = '\033[92m'
BLUE = '\033[94m'
RESET = '\033[0m'


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
    text += "  ./encode.py --cfg enc.cfg\n"
    text += "  ./encode.py --cfg enc.cfg --input video.yuv --output video.hevc\n"
    text += "  ./encode.py --cfg enc.cfg --first-picture 10 --max-picture 100\n"
    text += "\n"

    parser = argparse.ArgumentParser(description=text, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cfg", "-c", required=True, help="Input configuration file path")
    parser.add_argument("--input", "-i", help="Input YUV file (overrides config YUVFile)")
    parser.add_argument("--output", "-o", help="Output bitstream file (overrides config BitstreamFile)")
    parser.add_argument("--first-picture", "-f", type=int, help="First picture index (overrides config FirstPicture)")
    parser.add_argument("--max-picture", "-m", help="Max pictures to encode, or 'ALL' (overrides config MaxPicture)")

    args = parser.parse_args()

    # Parse configuration file
    config = vcu_config_parser.VCUConfigParser()
    config.parse(args.cfg)
    encoder_params = config.create_encoder_params()

    # Get input file from command line or config
    input_file = args.input
    if not input_file:
        if 'INPUT' in config.sections and 'YUVFile' in config.sections['INPUT']:
            input_file = config.sections['INPUT']['YUVFile']
        else:
            print(f"{BLUE}Error: No input file specified. Use --input or set YUVFile in config.{RESET}")
            sys.exit(1)

    # Get output file from command line or config
    output_file = args.output
    if not output_file:
        if 'OUTPUT' in config.sections and 'BitstreamFile' in config.sections['OUTPUT']:
            output_file = config.sections['OUTPUT']['BitstreamFile']
        else:
            print(f"{BLUE}Error: No output file specified. Use --output or set BitstreamFile in config.{RESET}")
            sys.exit(1)

    # Get first picture from command line or config [RUN] section
    first_picture = args.first_picture
    if first_picture is None:
        if 'RUN' in config.sections and 'FirstPicture' in config.sections['RUN']:
            first_picture = int(config.sections['RUN']['FirstPicture'])
        else:
            first_picture = 0

    # Get max picture from command line or config [RUN] section
    max_picture_value = args.max_picture
    if max_picture_value is None:
        if 'RUN' in config.sections and 'MaxPicture' in config.sections['RUN']:
            max_picture_value = config.sections['RUN']['MaxPicture']
    max_picture = parse_max_picture(max_picture_value)

    # Print encoding parameters
    pic = encoder_params.pictureEncSettings
    rc = encoder_params.rcSettings
    gop = encoder_params.gopSettings

    print(f"\n{GREEN}VCU Encoder{RESET}")
    print(f"  Input:   {input_file}")
    print(f"  Output:  {output_file}")
    print(f"  Size:    {pic.width}x{pic.height}")
    print(f"  Codec:   {'HEVC' if pic.codec == vcu.CODEC_HEVC else 'AVC'}")
    print(f"  Bitrate: {rc.bitrate // 1000} kbps")
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
    print(f"Encoding {input_file}...")
    try:
        encoder.writeFile(input_file, first_picture, max_picture)
    except Exception as e:
        print(f"{BLUE}Error during encoding: {e}{RESET}")
        sys.exit(1)

    # Signal end of stream and wait for completion
    encoder.eos()

    print(f"{GREEN}Encoding complete: {output_file}{RESET}")


if __name__ == "__main__":
    main()


