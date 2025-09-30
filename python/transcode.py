#! /usr/bin/env python3
import cv2
from time import sleep
from formats import FOURCC
decoderInitParams = cv2.vcucodec.DecoderInitParams(
    codec=cv2.vcucodec.CODEC_AVC,
    fourcc=FOURCC("NV12"),
    fourccConvert=0, # FOURCC("BGR"),
    maxFrames=0,
    bitDepth=cv2.vcucodec.BIT_DEPTH_FIRST)

dec = cv2.vcucodec.createDecoder('./meet_car_720p_nv12.avc', decoderInitParams)
params = cv2.vcucodec.EncoderInitParams()
enc = cv2.vcucodec.createEncoder("./out.h265", params)

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

del dec
del enc

