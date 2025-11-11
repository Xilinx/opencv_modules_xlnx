#! /usr/bin/env python3
import cv2
import sys
import argparse
from formats import *
import vcu_config_parser

GREEN = '\033[92m'
BLUE = '\033[94m'
RESET = '\033[0m'

text = "AVC/HEVC Encoder\n"
text += "\n\nExample usage:\n"
text += "  ./encode.py -cfg enc.cfg \n"
text += "\n\n"
parser = argparse.ArgumentParser(description=text, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--cfg", required=True, help="Input configuration file path")

args = parser.parse_args()

config = vcu_config_parser.VCUConfigParser()
config.parse(args.cfg)
encoderInitParams = config.create_encoder_params()
print(members_str(encoderInitParams));


