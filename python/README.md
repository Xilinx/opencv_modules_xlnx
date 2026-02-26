# VCU Python Scripts

Python scripts for hardware-accelerated video encoding and decoding using the VCU (Video Codec Unit).

## decode.py

Decode AVC/HEVC bitstreams to raw YUV files using VCU/VCU2 OpenCV API.

**Usage:**
```bash
./decode.py --avc --input video.264 --output video.yuv
./decode.py --hevc --input video.hevc --output video.yuv
./decode.py --hevc --input video.hevc --output video --output-format NV12
./decode.py --hevc --input video.hevc --output video.bgr --convert BGR
./decode.py --hevc --input video.hevc --output video.yuv --bitdepth 10
./decode.py --hevc --input video.hevc --output video.yuv --zero-copy
```

**Options:**
| Option | Description |
|--------|-------------|
| `--avc` | Decode AVC/H.264 bitstream (mutually exclusive with `--hevc`) |
| `--hevc` | Decode HEVC/H.265 bitstream (mutually exclusive with `--avc`) |
| `--input`, `-i` | Input bitstream file path (required) |
| `--output`, `-o` | Output file path (required) |
| `--output-format` | Output YUV format (default: NULL for auto-detect) |
| `--convert` | Color space conversion: `BGR` or `BGRA` |
| `--max-frames` | Maximum number of frames to decode (0 = unlimited) |
| `--bitdepth`, `-bd` | Output bit depth: `8`, `10`, `12`, `alloc`, `stream`, or `first` (default) |
| `--no-yuv` | Disable YUV output (decode only, for benchmarking) |
| `--zero-copy` | Use zero-copy numpy access (no data copy from HW buffer) |

**Bit Depth Options:**
- `first` - Use bit depth of first decoded frame (default)
- `alloc` - Force preallocated bit depth if present, otherwise fallback to first
- `stream` - Use current frame's bit depth
- `8`, `10`, `12` - Force specific bit depth

## transcode.py

Transcode AVC/HEVC bitstreams to AVC/HEVC bitstreams using VCU/VCU2 OpenCV API. This script
combines decoding and encoding in a single pipeline, reading frames from the decoder and
immediately feeding them to the encoder.

**Usage:**
```bash
./transcode.py --avc --input video.264 --output video_out.264
./transcode.py --hevc --input video.hevc --output video_out.hevc
./transcode.py --hevc --input video.hevc --output video_out.hevc --output-format NV12
./transcode.py --hevc --input video.hevc --output video_out.hevc --max-frames 100
```

**Options:**
| Option | Description |
|--------|-------------|
| `--avc` | Use AVC/H.264 codec (mutually exclusive with `--hevc`) |
| `--hevc` | Use HEVC/H.265 codec (mutually exclusive with `--avc`) |
| `--input`, `-i` | Input bitstream file path (required) |
| `--output`, `-o` | Output bitstream file path (required) |
| `--output-format` | Intermediate YUV format (default: NULL for auto-detect) |
| `--max-frames` | Maximum number of frames to transcode (0 = unlimited) |
| `--bitdepth`, `-bd` | Output bit depth: `8`, `10`, `12`, `alloc`, `stream`, or `first` (default) |

## encode.py

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

**Configuration File:**

The encoder uses a configuration file (e.g., `enc.cfg`) with the following sections:

- `[INPUT]` - Input file settings (YUVFile, Width, Height, Format, Framerate)
- `[OUTPUT]` - Output file settings (BitstreamFile)
- `[SETTINGS]` - Encoder settings (Profile, Level, Tier, NumSlices, etc.)
- `[GOP]` - GOP structure (GopCtrlMode, Gop.Length, Gop.NumB, Gop.FreqIDR, etc.)
- `[RATE_CONTROL]` - Rate control (RateCtrlMode, Bitrate, MaxBitrate, CPBSize, etc.)
- `[RUN]` - Runtime settings (FirstPicture, MaxPicture)

See `enc.cfg` for a complete example. This configuration file supports a subset of the
configuration file specified in:
[VCU2 Control Software configuration file](https://docs.amd.com/r/en-US/pg447-vcu2-solutions/Application-Software-Control-Software).

## Dependencies

- OpenCV with VCU codec module (`cv2.vcucodec`)
- VCU/VCU2 hardware and drivers
