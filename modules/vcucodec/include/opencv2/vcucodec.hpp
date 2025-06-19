// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.

#ifndef OPENCV_VCUCODEC_HPP
#define OPENCV_VCUCODEC_HPP

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <string>
#include <stdexcept>

/**
  @addtogroup vcu
  @{
    @defgroup vcucodec VCU Encoding/Decoding
  @}
**/

namespace cv {
namespace vcucodec {

//! @addtogroup vcucodec
//! @{

// Minimal definitions for testing
enum CodecType
{
    VCU_AVC = 0,     ///< AVC/H.264 codec
    VCU_HEVC = 1,    ///< HEVC/H.265 codec
};

/// Enum VCUFourCC defines the FourCC codes for various pixel formats supported by VCU.
/// Not all formats are output/input by the VCU hardware and will require software conversion.
/// The tiled formats are 32x4 (8 4x4) or 64x4 (16 4x4) tiles.
/// Pixel components are packed in a tile. Extra bits needed for 10/12 bit formats beyond 8 bits
/// are packed in the LSb of the next byte, thus offsetting the next pixel component.
/// 5 bytes are needed for 4 pixels in 10 bit format, and 6 bytes for 4 pixels in 12 bit format.
/// For a 4x4; 16 bytes for 8 bit, 20 bytes for 10 bit and 24 bytes for 12 bit format.
/// Each 32x4 tile is 128 bytes for 8 bit, 160 bytes for 10 bit and 192 bytes for 12 bit.
/// Each 64x4 tile is 256 bytes for 8 bit, 320 bytes for 10 bit and 384 bytes for 12 bit.
/// A tile is stored linearly in memory, with the first tile starting at offset 0.
enum VCUFourCC
{
    VCU_I0AL,     ///< 10 bit 4:2:0 planar (no hw support)
    VCU_I0CL,     ///< 12 bit 4:2:0 planar (no hw support)
    VCU_I2AL,     ///< 10 bit 4:2:2 planar (no hw support)
    VCU_I2CL,     ///< 12 bit 4:2:2 planar (no hw support)
    VCU_I420,     ///<  8 bit 4:2:0 planar (no hw support)
    VCU_I422,     ///<  8 bit 4:2:2 planar (no hw support)
    VCU_I444,     ///<  8 bit 4:4:4 planar
    VCU_I4AL,     ///< 10 bit 4:4:4 planar
    VCU_I4CL,     ///< 12 bit 4:4:4 planar
    VCU_IYUV,     ///< Same as I420 (no hw support)
    VCU_NV12,     ///<  8 bit 4:2:0 semi-planar
    VCU_NV16,     ///<  8 bit 4:2:2 semi-planar
    VCU_NV24,     ///<  8 bit 4:4:4 semi-planar (no hw support)
    VCU_P010,     ///< 10 bit 4:2:0 semi-planar
    VCU_P012,     ///< 12 bit 4:2:0 semi-planar
    VCU_P016,     ///< 16 bit 4:2:0 semi-planar (MSb)
    VCU_P210,     ///< 10 bit 4:2:2 semi-planar
    VCU_P212,     ///< 12 bit 4:2:2 semi-planar
    VCU_P216,     ///< 16 bit 4:2:2 semi-planar (MSb) (no hw support)
    VCU_P410,     ///< 10 bit 4:4:4 semi-planar (no hw support)
    VCU_Y010,     ///< 10 bit 4:0:0 single-plane
    VCU_Y012,     ///< 12 bit 4:0:0 single-plane
    VCU_I4AM,     ///< xx bit 4:4:4 planar (MSb)
    VCU_Y800,     ///<  8 bit 4:0:0 single-plane
    VCU_YUVP,     ///< 10 bit 4:2:2 packed (MSb) (no hw support)
    VCU_YUY2,     ///<  8 bit 4:2:2 packed (MSb) (no hw support)
    VCU_YV12,     ///< Same as I420 with inverted U and V order (no hw support)
    VCU_YV16,     ///< Same as I422 (no hw support)
    VCU_T508,     ///<  8 bit 4:2:0 semi-planar tiled
    VCU_T50A,     ///< 10 bit 4:2:0 semi-planar tiled
    VCU_T50C,     ///< 12 bit 4:2:0 semi-planar tiled
    VCU_T528,     ///<  8 bit 4:2:2 semi-planar tiled
    VCU_T52A,     ///< 10 bit 4:2:2 semi-planar tiled
    VCU_T52C,     ///< 12 bit 4:2:2 semi-planar tiled
    VCU_T548,     ///<  8 bit 4:4:4 planar tiled
    VCU_T54A,     ///< 10 bit 4:4:4 planar tiled
    VCU_T54C,     ///< 12 bit 4:4:4 planar tiled
    VCU_T5M8,     ///<  8 bit 4:0:0 single-plane tiled
    VCU_T5MA,     ///< 10 bit 4:0:0 single-plane tiled
    VCU_T5MC,     ///< 12 bit 4:0:0 single-plane tiled
    VCU_T608,     ///<  8 bit 4:2:0 semi-planar tiled
    VCU_T60A,     ///< 10 bit 4:2:0 semi-planar tiled
    VCU_T60C,     ///< 12 bit 4:2:0 semi-planar tiled
    VCU_T628,     ///<  8 bit 4:2:2 semi-planar tiled
    VCU_T62A,     ///< 10 bit 4:2:2 semi-planar tiled
    VCU_T62C,     ///< 12 bit 4:2:2 semi-planar tiled
    VCU_T648,     ///<  8 bit 4:4:4 planar tiled
    VCU_T64A,     ///< 10 bit 4:4:4 planar tiled
    VCU_T64C,     ///< 12 bit 4:4:4 planar tiled
    VCU_T6M8,     ///<  8 bit 4:0:0 single-plane tiled
    VCU_T6MA,     ///< 10 bit 4:0:0 single-plane tiled
    VCU_T6MC,     ///< 12 bit 4:0:0 single-plane tiled

    VCU_AUTO,     ///< auto detect format
};

// Function declarations (moved to source file)
CV_EXPORTS_W String fourccToString(VCUFourCC fourcc);
CV_EXPORTS_W VCUFourCC stringToFourcc(const String& str);

/// Minimal struct for testing
struct CV_EXPORTS_W_SIMPLE RawInfo {
    CV_PROP_RW int width;
    CV_PROP_RW int height;
};

/// Struct DecoderInitParams contains initialization parameters for the decoder.
struct CV_EXPORTS_W_SIMPLE DecoderInitParams
{
    CV_PROP_RW CodecType codecType;        ///< codec type (AVC, HEVC, JPEG)
    CV_PROP_RW VCUFourCC outputFormat;     ///< format of the raw data as FourCC code, default is AUTO

    DecoderInitParams() : codecType(VCU_HEVC), outputFormat(VCU_AUTO) {}
};

/// Minimal decoder class
class CV_EXPORTS_W Decoder
{
public:
    virtual ~Decoder() {}
    CV_WRAP virtual bool nextFrame(OutputArray frame, RawInfo& frame_info) = 0;
};

/// Struct EncoderParams contains encoder parameters and statistics
struct CV_EXPORTS_W_SIMPLE EncoderInitParams {
    CV_PROP_RW CodecType codecType;        ///< codec type (AVC, HEVC, JPEG)
    CV_PROP_RW VCUFourCC outputFormat;     ///< format of the raw data as FourCC code, default is AUTO
    CV_PROP_RW int bitrate;                ///< target bitrate in kbits per second
    CV_PROP_RW int frameRate;              ///< frame rate
    CV_PROP_RW int gopLength;              ///< GOP (Group of Pictures) length

    EncoderInitParams() : codecType(VCU_HEVC), outputFormat(VCU_AUTO), 
                         bitrate(4000), frameRate(30), gopLength(60) {}
};

/// Minimal encoder class  
class CV_EXPORTS_W Encoder
{
public:
    virtual ~Encoder() {}
    CV_WRAP virtual void write(InputArray frame) = 0;
};

// Simple factory functions
CV_EXPORTS_W Ptr<Decoder> createDecoder(const String& filename, const DecoderInitParams& params = DecoderInitParams());
CV_EXPORTS_W Ptr<Encoder> createEncoder(const String& filename, const EncoderInitParams& params = EncoderInitParams());
 
//! @}
}  // namespace vcucodec
}  // namespace cv

#endif // OPENCV_VCUCODEC_HPP
