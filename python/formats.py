#! /usr/bin/env python3
import cv2

def FOURCC(str_fourcc):
    str_fourcc = str_fourcc[:4].ljust(4)  # ensure exactly 4 characters (space padded)
    return cv2.VideoWriter_fourcc(*str_fourcc)

def fourcc_to_string(fourcc):
    """Convert FourCC integer to readable string"""
    return "".join([chr((fourcc >> 8 * i) & 0xFF) for i in range(4)])

def bitdepth_str_to_enum(bd_str):
    if bd_str == "8":
        return cv2.vcucodec.BIT_DEPTH_8
    elif bd_str == "10":
        return cv2.vcucodec.BIT_DEPTH_10
    elif bd_str == "12":
        return cv2.vcucodec.BIT_DEPTH_12
    elif bd_str == "alloc":
        return cv2.vcucodec.BIT_DEPTH_ALLOC
    elif bd_str == "stream":
        return cv2.vcucodec.BIT_DEPTH_STREAM
    elif bd_str == "first":
        return cv2.vcucodec.BIT_DEPTH_FIRST
    else:
        raise ValueError(f"Invalid bitdepth string: {bd_str}")

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
            if callable(value):
                continue
            if attr in ['fourcc', 'fourccConvert'] and value != 0:
                value = fourcc_to_string(value)
            if str(type(value)).startswith("<class 'cv2."):
                attrs.append(f"{attr}: {members_str(value)}")
            else:
                attrs.append(f"{attr}: {value}")
    return f"{obj.__class__.__name__}{{{', '.join(attrs)}}}"

def writeyuv(f, frame, width, height, stride):
    """Write YUV data to a file in NV12 format."""
    # Write Y plane
    for y in range(height):
        f.write(frame[y, :width].tobytes())
    # Write UV plane
    for y in range(height // 2):
        f.write(frame[height + y, :width].tobytes())

def writeyuv_planar(f, frame, width, height, stride, bit_depth):
    """Write YUV data to a file in I420 format."""
    bytes_per_sample = 1 if bit_depth <= 8 else 2
    # Write Y plane
    for y in range(height):
        f.write(frame[y, :width * bytes_per_sample].tobytes())
    # Write U plane
    for y in range(height // 2):
        f.write(frame[height + y, : (width // 2) * bytes_per_sample].tobytes())
    # Write V plane
    for y in range(height // 2):
        f.write(frame[height + (height // 2) + y, : (width // 2) * bytes_per_sample].tobytes())

def writebgr(f, frame):
    """Write BGR data to a file."""
    for y in range(frame.shape[0]):
        f.write(frame[y, :, :].tobytes())


def write(f, frame, info):
    """Write frame data to a file based on its format."""
    if info.fourcc == FOURCC("NV12"):
        writeyuv(f, frame, info.width, info.height, info.stride)
    elif info.fourcc == FOURCC("I420"):
        writeyuv_planar(f, frame, info.width, info.height, info.stride, 8)
    else:
        raise ValueError(f"Unsupported FourCC format: {fourcc_to_string(info.fourcc)}")

