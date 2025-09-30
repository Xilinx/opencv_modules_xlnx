#! /usr/bin/env python3
import cv2

def FOURCC(str_fourcc):
    str_fourcc = str_fourcc[:4].ljust(4)  # ensure exactly 4 characters (space padded)
    return cv2.VideoWriter_fourcc(*str_fourcc)

def fourcc_to_string(fourcc):
    """Convert FourCC integer to readable string"""
    return "".join([chr((fourcc >> 8 * i) & 0xFF) for i in range(4)])

def showprops(dec):
    prop = dec.get(cv2.CAP_PROP_FOURCC)
    print(f"Initial FourCC: {fourcc_to_string(int(prop))}")
    prop = dec.get(cv2.CAP_PROP_CODEC_PIXEL_FORMAT)
    print(f"Initial Pixel Format: {fourcc_to_string(int(prop))}")
    prop = dec.get(cv2.CAP_PROP_POS_FRAMES)
    print(f"Initial Position: {int(prop)}")
    w = int(dec.get(cv2.CAP_PROP_FRAME_WIDTH))
    h = int(dec.get(cv2.CAP_PROP_FRAME_HEIGHT))
    print(f"Initial Frame Size: {w}x{h}")


def members_str(obj) -> str:
    """Print all member variables of a class instance."""
    attrs = []
    for attr in dir(obj):
        if not attr.startswith("_"):
            value = getattr(obj, attr)
            if attr in ['fourcc', 'fourccConvert'] and value != 0:
                value = fourcc_to_string(value)
            attrs.append(f"{attr}: {value}")
    return f"{obj.__class__.__name__}: {', '.join(attrs)}"
