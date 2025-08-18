/*
   Copyright (c) 2025  Advanced Micro Devices, Inc. (AMD)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef OPENCV_VCUCODEC_HPP
#define OPENCV_VCUCODEC_HPP

#include "vcutypes.hpp"
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>


#include <string>
#include <stdexcept>

/**
  @addtogroup versal_zynq
  @{
    @defgroup vcucodec VCU Encoding/Decoding
    @brief VCU codec module for encoding and decoding video streams on AMD/Xilinx Versal and
           Zynq UltraScale+ MPSoC platforms

    This module provides support for encoding and decoding video streams using the VCU
    (Video Codec Unit)
  @}
**/

namespace cv {
namespace vcucodec {

//! @addtogroup vcucodec
//! @{

/// Information about a raw YUV frame containing metadata such as format, dimensions, and stride.
struct CV_EXPORTS_W_SIMPLE RawInfo {
    CV_PROP_RW bool eos;            ///< End-of-stream flag, below information valid only if false
    CV_PROP_RW int  fourcc;         ///< Output format as FOURCC code
    CV_PROP_RW int  bitsPerLuma;    ///< Bit depth of the output data, 8, 10, or 12 bits per channel
    CV_PROP_RW int  bitsPerChroma;  ///< Bit depth of the output data, 8, 10, or 12 bits per channel
    CV_PROP_RW int  stride;         ///< Stride of the output frame in bytes
    CV_PROP_RW int  width;          ///< Width of the raw frame
    CV_PROP_RW int  height;         ///< Height of the raw frame
    CV_PROP_RW int  posX;           ///< Position x offset
    CV_PROP_RW int  posY;           ///< Position y offset
    CV_PROP_RW int  cropTop;        ///< Crop top offset
    CV_PROP_RW int  cropBottom;     ///< Crop bottom offset
    CV_PROP_RW int  cropLeft;       ///< Crop left offset
    CV_PROP_RW int  cropRight;      ///< Crop right offset

    CV_PROP_RW PicStruct picStruct; ///< Picture structure (frame, top/bottom field, ...)

};

/// Struct DecoderInitParams contains initialization parameters for the decoder.
struct CV_EXPORTS_W_SIMPLE DecoderInitParams
{
    CV_PROP_RW Codec codec;       ///< Codec type (AVC, HEVC, JPEG)
    CV_PROP_RW int fourcc;        ///< Format of the output raw data as FOURCC code,
                                  ///< Default is VCU_FOURCC_AUTO (determined automatically)
    CV_PROP_RW int fourccConvert; ///< FOURCC specifying to convert to BGR or BGRA, or 0 (none)
    CV_PROP_RW int maxFrames;     ///< Maximum number of frames to decode, 0 for unlimited
    CV_PROP_RW BitDepth bitDepth; ///< Specify output bit depth (first, alloc, stream, 8, 10, 12)

    /// Constructor to initialize decoder parameters with default values.
    CV_WRAP DecoderInitParams(Codec codec = Codec::HEVC, int fourcc = VCU_FOURCC_AUTO,
        int fourccConvert = 0, int maxFrames = 0, BitDepth bitDepth = BitDepth::ALLOC)
        : codec(codec), fourcc(fourcc), fourccConvert(fourccConvert),
          maxFrames(maxFrames), bitDepth(bitDepth)
    {}
};

/// Decoder interface for decoding video streams
/// This interface provides methods to decode video frames from a stream.
/// See: @ref dec_python_ex
class CV_EXPORTS_W Decoder
{
public:
    /// Virtual destructor for the Decoder interface.
    virtual ~Decoder() {}

    /// Decode the next frame from the stream.
    /// @return true if a frame was successfully decoded, false if no frames are available (yet)
    //          or if an error occurred.
    CV_WRAP virtual bool nextFrame(
        CV_OUT OutputArray frame, ///< Output array to store the decoded frame
        CV_OUT RawInfo& frameInfo ///< Output parameter with information about the decoded frame
    ) = 0;

    /// Decode the next frame from the stream into separate planes; does not support conversion to
    /// BGR or BGRA (DecoderInitParams.fourccConvert is ignored)
    /// @return true if a frame was successfully decoded, false if no frames are available (yet)
    //          or if an error occurred.
    CV_WRAP virtual bool nextFramePlanes(
        CV_OUT OutputArrayOfArrays planes, ///< Output array vector to store the decoded frame
        CV_OUT RawInfo& frameInfo     ///< Output parameter with information about the decoded frame
    ) = 0;


    /// Set a property for the decoder.
    /// @return true if the property was set successfully, false otherwise
    /// Properties that user can set:
    /// - None
    CV_WRAP virtual bool set(
        int propId,  ///< Property identifier
        double value ///< Value to set for the property
    ) = 0;

    /// Get the value of a property.
    /// Provided properties:
    /// - CAP_PROP_FOURCC: The codec type (H264, HEVC, MJPG)
    /// - CAP_PROP_CODEC_PIXEL_FORMAT: The pixel format of the decoded frames (NV12, ...)
    /// - CAP_PROP_FRAME_WIDTH: Width of the decoded frames
    /// - CAP_PROP_FRAME_HEIGHT: Height of the decoded frames
    /// - CAP_PROP_POS_FRAMES: Current frame position in the stream
    CV_WRAP virtual double get(
        int propId ///< Property identifier
    ) const = 0;
};

/// Struct EncoderParams contains encoder parameters and statistics
struct CV_EXPORTS_W_SIMPLE EncoderInitParams {
    CV_PROP_RW Codec codec;    ///< Codec type (AVC, HEVC, JPEG)
    CV_PROP_RW int fourcc;     ///< Format of the raw data as FOURCC code
    CV_PROP_RW int bitrate;    ///< Target bitrate in kbits per second
    CV_PROP_RW int frameRate;  ///< Frame rate
    CV_PROP_RW int gopLength;  ///< GOP (Group of Pictures) length

    /// Constructor to initialize encoder parameters with default values.
    CV_WRAP EncoderInitParams(Codec codec = Codec::HEVC,
            int fourcc = VideoWriter::fourcc('N', 'V', '1', '2'),
            int bitrate = 4000, int frameRate = 30, int gopLength = 60)
        : codec(codec), fourcc(fourcc), bitrate(bitrate), frameRate(frameRate),
          gopLength(gopLength) {}
};

/// Encoder interface for encoding video frames to a stream.
/// This interface provides methods to encode video frames and manage encoding parameters.
class CV_EXPORTS_W Encoder
{
public:
    /// Virtual destructor for the Encoder interface.
    virtual ~Encoder() {}

    /// Encode a video frame.
    CV_WRAP virtual void write(InputArray frame) = 0;

    /// Set a property for the encoder.
    /// @return true if the property was set successfully, false otherwise
    CV_WRAP virtual bool set(
        int propId,  ///< Property identifier
        double value ///< Value to set for the property
    ) = 0;

    /// Get the value of a property.
    CV_WRAP virtual double get(
        int propId ///< Property identifier
    ) const = 0;
};

/// Factory function to create a decoder instance.
CV_EXPORTS_W Ptr<Decoder> createDecoder(
    const String& filename,                               ///< Onput video file name or stream URL
    const DecoderInitParams& params = DecoderInitParams() ///< Decoder initialization parameters
);

/// Factory function to create a decoder instance.
CV_EXPORTS_W Ptr<Encoder> createEncoder(
    const String& filename,                               ///< Output video file name or stream URL
    const EncoderInitParams& params = EncoderInitParams() ///< Encoder initialization parameters
);

//! @}
}  // namespace vcucodec
}  // namespace cv

#endif // OPENCV_VCUCODEC_HPP
