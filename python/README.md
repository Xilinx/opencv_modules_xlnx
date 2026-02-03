# VCU Python Scripts

Python scripts for hardware-accelerated video encoding and decoding using the VCU (Video Codec Unit).

## Scripts

### encode.py

Encode raw YUV files to AVC (H.264) or HEVC (H.265) bitstreams using VCU/VCU2 OpenCV API.

**Usage:**
```bash
./encode.py --hevc --cfg enc.cfg --input video.yuv --output video.hevc
./encode.py --avc --cfg enc.cfg --input video.yuv --output video.264
./encode.py --hevc --cfg enc.cfg --first-picture 10 --max-picture 100
./encode.py --show                # show defaults only
./encode.py --show --cfg enc.cfg  # show defaults and config values
```

**Options:**
| Option | Description |
|--------|-------------|
| `--show` | Show encoder parameters (defaults, or with `--cfg` also config values) |
| `--avc` | Encode using AVC/H.264 codec |
| `--hevc` | Encode using HEVC/H.265 codec (default) |
| `--cfg`, `-c` | Input configuration file path |
| `--input`, `-i` | Input YUV file (overrides config YUVFile) |
| `--output`, `-o` | Output bitstream file (overrides config BitstreamFile) |
| `--first-picture`, `-f` | First picture index (overrides config FirstPicture) |
| `--max-picture`, `-m` | Max pictures to encode, or 'ALL' (overrides config MaxPicture) |
| `--quiet`, `-q` | Suppress output messages |

### decode.py

Decode AVC/HEVC bitstreams to raw YUV files.

### transcode.py

Transcode video files (decode + encode).

## Configuration File

The encoder uses a configuration file (e.g., `enc.cfg`) with the following sections:

- `[INPUT]` - Input file settings (YUVFile, Width, Height, Format, Framerate)
- `[OUTPUT]` - Output file settings (BitstreamFile)
- `[SETTINGS]` - Encoder settings (Profile, Level, Tier, NumSlices, etc.)
- `[GOP]` - GOP structure (GopCtrlMode, Gop.Length, Gop.NumB, Gop.FreqIDR, etc.)
- `[RATE_CONTROL]` - Rate control (RateCtrlMode, Bitrate, MaxBitrate, CPBSize, etc.)
- `[RUN]` - Runtime settings (FirstPicture, MaxPicture)

See `enc.cfg` for a complete example.

This configuration file is similar to the [VCU2 Control Software configuration file]
(https://docs.amd.com/r/en-US/pg447-vcu2-solutions/Application-Software-Control-Software).

## Dependencies

- OpenCV with VCU codec module (`cv2.vcucodec`)
- VCU hardware and drivers
