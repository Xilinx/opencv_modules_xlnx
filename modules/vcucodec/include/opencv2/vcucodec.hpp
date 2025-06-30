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

static int VCU_FOURCC_AUTO = 0; ///< Auto-detect format


// Minimal definitions for testing
enum CodecType
{
    VCU_AVC = 0,     ///< AVC/H.264 codec
    VCU_HEVC = 1,    ///< HEVC/H.265 codec
    VCU_JPEG = 2,    ///< JPEG only (VCU2 and decode only)
};


/// Minimal struct for testing
struct CV_EXPORTS_W_SIMPLE RawInfo {
    CV_PROP_RW int width;
    CV_PROP_RW int height;
};

/// Struct DecoderInitParams contains initialization parameters for the decoder.
struct CV_EXPORTS_W_SIMPLE DecoderInitParams
{
    CV_PROP_RW CodecType codecType;  ///< codec type (AVC, HEVC, JPEG)
    CV_PROP_RW int fourcc;           ///< format of the raw data as FourCC code, default is VCU_FOURCC_AUTO

    DecoderInitParams() : codecType(VCU_HEVC), fourcc(VCU_FOURCC_AUTO) {}
};

/// Minimal decoder class
class CV_EXPORTS_W Decoder
{
public:
    virtual ~Decoder() {}
    CV_WRAP virtual bool nextFrame(OutputArray frame, RawInfo& frame_info) = 0;
    CV_WRAP virtual bool set(int propId, double value) = 0;
    CV_WRAP virtual double get(int propId) const = 0;
};

/// Struct EncoderParams contains encoder parameters and statistics
struct CV_EXPORTS_W_SIMPLE EncoderInitParams {
    CV_PROP_RW CodecType codecType;        ///< codec type (AVC, HEVC, JPEG)
    CV_PROP_RW int fourcc;                 ///< format of the raw data as FourCC code
    CV_PROP_RW int bitrate;                ///< target bitrate in kbits per second
    CV_PROP_RW int frameRate;              ///< frame rate
    CV_PROP_RW int gopLength;              ///< GOP (Group of Pictures) length

    EncoderInitParams() : codecType(VCU_HEVC), fourcc(VideoWriter::fourcc('N', 'V', '1', '2')),
                         bitrate(4000), frameRate(30), gopLength(60) {}
};

/// Minimal encoder class
class CV_EXPORTS_W Encoder
{
public:
    virtual ~Encoder() {}
    CV_WRAP virtual void write(InputArray frame) = 0;
    CV_WRAP virtual bool set(int propId, double value) = 0;
    CV_WRAP virtual double get(int propId) const = 0;
};

// Simple factory functions
CV_EXPORTS_W Ptr<Decoder> createDecoder(const String& filename, const DecoderInitParams& params = DecoderInitParams());
CV_EXPORTS_W Ptr<Encoder> createEncoder(const String& filename, const EncoderInitParams& params = EncoderInitParams());

//! @}
}  // namespace vcucodec
}  // namespace cv

#endif // OPENCV_VCUCODEC_HPP
