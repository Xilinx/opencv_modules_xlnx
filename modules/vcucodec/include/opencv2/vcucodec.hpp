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

/// Struct RawInfo defines a raw YUV frame containing metadata such as format, dimensions, and stride.
struct CV_EXPORTS_W_SIMPLE RawInfo {
    CV_PROP_RW bool eos;            ///< End-of-stream flag, below information valid only if false
    CV_PROP_RW int  fourcc;         ///< Output format as FOURCC code
    CV_PROP_RW int  bitsPerLuma;    ///< Bit depth of the output data, 8, 10, or 12 bits per channel
    CV_PROP_RW int  bitsPerChroma;  ///< Bit depth of the output data, 8, 10, or 12 bits per channel
    CV_PROP_RW int  stride;         ///< Stride of the output frame in bytes
    CV_PROP_RW int  strideChroma;   ///< Stride of Chroma buffer of the output frame in bytes.
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
    CV_PROP_RW int szReturnQueue; ///< Return queue size when returning frames by reference.
                                  ///< Minimum/Default (0), when set to 0 frames cannot be returned
                                  ///< by reference.

    /// Constructor to initialize decoder parameters with default values.
    CV_WRAP DecoderInitParams(Codec codec = Codec::HEVC, int fourcc = VCU_FOURCC_AUTO,
        int fourccConvert = 0, int maxFrames = 0, BitDepth bitDepth = BitDepth::ALLOC);
};

/// @brief Class Decoder is the interface for decoding video streams.
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

    /// Decode the next frame from the stream into separate planes.
    /// When called to get frame n by reference, frame n - szReturnQueue is unreferenced.
    /// @return true if a frame was successfully decoded, false if no frames are available (yet)
    //          or if an error occurred.
    CV_WRAP virtual bool nextFramePlanes(
        CV_OUT OutputArrayOfArrays planes, ///< Output array vector to store the decoded frame
        CV_OUT RawInfo& frameInfo,  ///< Output parameter with information about the decoded frame
        bool byRef = false   ///< Return frame by reference instead of a copy.
                             ///< When set to true, szReturnQueue must be >= 1.
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

    /// Get the information of the stream that was parsed (if any sofar).
    CV_WRAP virtual String streamInfo() const = 0;

    /// Get the statistics of the stream that was decoded (if any).
    CV_WRAP virtual String statistics() const = 0;
};

/// Struct RCSettings provides Rate Control Settings.
struct CV_EXPORTS_W_SIMPLE RCSettings
{
    CV_PROP_RW RCMode  mode;          ///< Rate control mode (default VBR)
    CV_PROP_RW Entropy entropy;       ///< Entropy coding mode (CAVLC or CABAC)
    CV_PROP_RW int  bitrate;          ///< Target bitrate in kbits per second
    CV_PROP_RW int  maxBitrate;       ///< Maximum bitrate in kbits per second
    CV_PROP_RW int  cpbSize;          ///< Coded Picture Buffer (CPB) size in milliseconds.
                                      ///< Cannot be smaller than initial-delay. Default: 3000.
    CV_PROP_RW int  initialDelay;     ///< Initial delay in milliseconds. Default: 1000.
    CV_PROP_RW bool fillerData;       ///< Add filler data in CBR mode. Default: true;
    CV_PROP_RW int  maxQualityTarget; ///< 0-20. Maximum quality target for CAPPED_VBR. Default: 14.
    CV_PROP_RW int  maxPictureSizeI;  ///< Maximum picture size in kBytes. Default: 0 (unlimited).
    CV_PROP_RW int  maxPictureSizeP;  ///< for CBR/VBR, for I, P, B
    CV_PROP_RW int  maxPictureSizeB;  ///< max = (bitrate/framerate) * allowed peak margin
    CV_PROP_RW bool skipFrame;        ///< Skip a frame when the CPB buffer size is exceeded and
                                      ///< replace with skip MBs (or CTBs). Default: false
    CV_PROP_RW int  maxSkip;          ///< Maximum number of skips in a row. Default: unlimited

    CV_WRAP RCSettings(RCMode mode = RCMode::VBR, Entropy entropy = Entropy::CABAC,
        int bitrate = 4000, int maxBitrate = 4000, int cbPSize = 3000, int initialDelay = 1000,
        bool fillerData = true, int maxQualityTarget = 14, int maxPictureSizeI = 0,
        int maxPictureSizeP = 0, int maxPictureSizeB = 0, bool skipFrame = false,  int maxSkip = -1);
};

/// Struct GOPSettings specifies the structure of the Group Of Pictures (GOP).
struct CV_EXPORTS_W_SIMPLE GOPSettings
{
    CV_PROP_RW GOPMode mode;      ///< Group of pictures mode.
    CV_PROP_RW GDRMode gdrMode;   ///< Gradual Decoder Refresh scheme used for low delay gop-mode
    CV_PROP_RW int  gopLength;    ///< Distance between two consecutive I-frames.
                                  ///< Default: 30. Range 0-1000. (0,1 is intra-only)
    CV_PROP_RW int  nrBFrames;    ///< Number of B-frames between two consecutive P-frames. For
                                  ///< basic and pyramidal modes. 0-4 for basic GOP mode.
                                  ///< 3,5, or 7 for pyramidal GOP mode. Default: 0.
    CV_PROP_RW bool longTermRef;  ///< Specify that a long-term reference can be dynamically
                                  ///< inserted. Default: false
    CV_PROP_RW int  longTermFreq; ///< Specify the periodicity in frames; the distance between two
                                  ///< consecutive long-term reference pictures. Default: 0.
    CV_PROP_RW int  periodIDR;    ///< The number of frames between consecutive Instantaneous
                                  ///< Decoder Refresh (IDR) pictures. This might be rounded to a
                                  ///< multiple of the GOP length.
                                  ///< -1 disables, 0 (default) first frame is IDR
    CV_WRAP GOPSettings(GOPMode mode = GOPMode::BASIC, GDRMode gdrMode = GDRMode::DISABLE,
        int gopLength = 30, int nrBFrames = 0, bool longTermRef = false,
        int longTermFreq = 0, int periodIDR = 0);
};

/// @brief Struct ProfileSettings specifies the encoder profile, level and tier.
struct CV_EXPORTS_W_SIMPLE ProfileSettings
{
    CV_PROP_RW String profile; ///< Encoder profile (e.g., "main", "high")
    CV_PROP_RW String level;   ///< Encoder level (e.g., 4.1, 5.0)
    CV_PROP_RW Tier   tier;    ///< Encoder tier (e.g., Main, High)

    CV_WRAP ProfileSettings(String profile = "MAIN", String level = "5.2", Tier tier = Tier::MAIN);
};

struct CV_EXPORTS_W_SIMPLE GlobalMotionVector
{
    CV_PROP_RW int frameIndex;   ///< Frame index
    CV_PROP_RW int gmVectorX;    ///< Global motion vector in X direction
    CV_PROP_RW int gmVectorY;    ///< Global motion vector in Y direction

    CV_WRAP GlobalMotionVector(int frameIndex = -1, int gmVectorX = 0, int gmVectorY = 0);
};

/// Struct EncoderInitParams contains encoder parameters and statistics
struct CV_EXPORTS_W_SIMPLE EncoderInitParams {
    CV_PROP_RW Codec codec;    ///< Codec type (AVC, HEVC, JPEG)
    CV_PROP_RW int fourcc;     ///< Format of the raw data as FOURCC code
    CV_PROP_RW RCMode rcMode;  ///< Rate control mode (CONST_QP, CBR, VBR, LOW_LATENCY, CAPPED_VBR)
    CV_PROP_RW int bitrate;    ///< Target bitrate in kbits per second
    CV_PROP_RW int pictWidth;  ///< Picture width
    CV_PROP_RW int pictHeight; ///< Picture height
    CV_PROP_RW int frameRate;  ///< Frame rate
    CV_PROP_RW int gopLength;  ///< GOP (Group of Pictures) length
    CV_PROP_RW int nrBFrames;  ///< GOP (Group of Pictures) Number of B-frames between two consecutive P-frames

    CV_PROP_RW ProfileSettings profileSettings; ///< Encoder profile, level and tier settings

    /// Constructor to initialize encoder parameters with default values.
    CV_WRAP EncoderInitParams(Codec codec = Codec::HEVC,
        int fourcc = VideoWriter::fourcc('N', 'V', '1', '2'), RCMode rcMode = RCMode::CBR,
        int bitrate = 4000, int pictWidth = 1280, int pictHeight = 720, int frameRate = 30,
        int gopLength = 60, int nrBFrames = 0);
};

/// @brief Class EncoderCallback reports encoder progress (not supported in Python).
class CV_EXPORTS_W EncoderCallback
{
public:
    virtual ~EncoderCallback() {}
    virtual void onEncoded(std::vector<std::string_view>& encodedData) = 0;
    virtual void onFinished() = 0;
};

/// @brief Class Encoder is the interface for encoding video frames to a stream.
/// This interface provides methods to encode video frames and manage encoding parameters.
class CV_EXPORTS_W Encoder
{
public:
    /// Virtual destructor for the Encoder interface.
    virtual ~Encoder() {}

    /// Encode a video frame.
    CV_WRAP virtual void write(InputArray frame) = 0;

    /// Signal the end of the stream to the encoder and wait until final frame is encoded.
    /// @return true if encoding completed successfully, false if timeout or error occurred
    CV_WRAP virtual bool eos() = 0;

    /// Get the statistics of the stream that was encoded (if any).
    CV_WRAP virtual String statistics() const = 0;

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


    /// Set rate control settings
    CV_WRAP virtual void set(const RCSettings& rcSettings) = 0;

    /// Get rate control settings
    CV_WRAP virtual void get(RCSettings& rcSettings) const = 0;

    /// Set GOP (Group Of Pictures) settings
    CV_WRAP virtual void set(const GOPSettings& gopSettings) = 0;

    /// Get GOP (Group Of Pictures) settings
    CV_WRAP virtual void get(GOPSettings& gopSettings) const = 0;

    /// Set global motion vector
    CV_WRAP virtual void set(const GlobalMotionVector& gmVector) = 0;

    /// Get global motion vector
    CV_WRAP virtual void get(GlobalMotionVector& gmVector) const = 0;

    /// Set profile, level and tier settings
    CV_WRAP virtual void set(const ProfileSettings& profileSettings) = 0;

    /// Get profile, level and tier settings
    CV_WRAP virtual void get(ProfileSettings& profileSettings) const = 0;

    //
    // Dynamic commands
    //

    /// Indicate a scene change at frameIdx with lookAhead frames
    CV_WRAP virtual void setSceneChange(int32_t frameIdx, int32_t lookAhead) = 0;

    /// Indicate that frameIdx is a long-term reference
    CV_WRAP virtual void setIsLongTerm(int32_t frameIdx) = 0;

    /// Indicate that frameIdx is a long-term reference
    CV_WRAP virtual void setUseLongTerm(int32_t frameIdx) = 0;

    /// Restart the GOP at frameIdx (next frame will be an IDR)
    CV_WRAP virtual void restartGop(int32_t frameIdx) = 0;

    /// Restart the GOP at frameIdx with a recovery point SEI
    CV_WRAP virtual void restartGopRecoveryPoint(int32_t frameIdx) = 0;

    /// Set the GOP length at frameIdx
    CV_WRAP virtual void setGopLength(int32_t frameIdx, int32_t gopLength) = 0;

    /// Set the number of B-frames at frameIdx
    CV_WRAP virtual void setNumB(int32_t frameIdx, int32_t numB) = 0;

    /// Set the frequency of IDR frames at frameIdx
    CV_WRAP virtual void setFreqIDR(int32_t frameIdx, int32_t freqIDR) = 0;

    /// Set the frame rate at frameIdx
    CV_WRAP virtual void setFrameRate(int32_t frameIdx, int32_t frameRate, int32_t clockRatio) = 0;

    /// Set the target and maximum bitrate at frameIdx
    CV_WRAP virtual void setBitRate(int32_t frameIdx, int32_t bitRate) = 0;

    /// Set the target and maximum bitrate at frameIdx
    CV_WRAP virtual void setMaxBitRate(int32_t frameIdx, int32_t iTargetBitRate, int32_t iMaxBitRate) = 0;

    /// Set the QP (Quantization Parameter) at a specific frame index.
    CV_WRAP virtual void setQP(int32_t frameIdx, int32_t qp) = 0;

    /// Set the QP offset at a specific frame index.
    CV_WRAP virtual void setQPOffset(int32_t frameIdx, int32_t iQpOffset) = 0;

    /// Set the QP bounds for a specific frame index.
    CV_WRAP virtual void setQPBounds(int32_t frameIdx, int32_t iMinQP, int32_t iMaxQP) = 0;

    /// Set the QP bounds for I-frames at a specific frame index.
    CV_WRAP virtual void setQPBoundsI(int32_t frameIdx, int32_t iMinQP_I, int32_t iMaxQP_I) = 0;

    /// Set the QP bounds for P-frames at a specific frame index.
    CV_WRAP virtual void setQPBoundsP(int32_t frameIdx, int32_t iMinQP_P, int32_t iMaxQP_P) = 0;

    /// Set the QP bounds for B-frames at a specific frame index.
    CV_WRAP virtual void setQPBoundsB(int32_t frameIdx, int32_t iMinQP_B, int32_t iMaxQP_B) = 0;

    /// Set the QP delta between I and P frames at a specific frame index.
    CV_WRAP virtual void setQPIPDelta(int32_t frameIdx, int32_t iQPDelta) = 0;

    /// Set the QP delta between P and B frames at a specific frame index.
    CV_WRAP virtual void setQPPBDelta(int32_t frameIdx, int32_t iQPDelta) = 0;

    /// Set the loop filter mode at a specific frame index.
    CV_WRAP virtual void setLFMode(int32_t frameIdx, int32_t iMode) = 0;

    /// Set the loop filter beta offset at a specific frame index.
    CV_WRAP virtual void setLFBetaOffset(int32_t frameIdx, int32_t iBetaOffset) = 0;

    /// Set the loop filter tc offset at a specific frame index.
    CV_WRAP virtual void setLFTcOffset(int32_t frameIdx, int32_t iTcOffset) = 0;

    /// Set the cost mode at a specific frame index.
    CV_WRAP virtual void setCostMode(int32_t frameIdx, bool bCostMode) = 0;

    /// Set the maximum picture size at a specific frame index.
    CV_WRAP virtual void setMaxPictureSize(int32_t frameIdx, int32_t iMaxPictureSize) = 0;

    /// Set the maximum picture size for I-frames at a specific frame index.
    CV_WRAP virtual void setMaxPictureSizeI(int32_t frameIdx, int32_t iMaxPictureSize_I) = 0;

    /// Set the maximum picture size for P-frames at a specific frame index.
    CV_WRAP virtual void setMaxPictureSizeP(int32_t frameIdx, int32_t iMaxPictureSize_P) = 0;

    /// Set the maximum picture size for B-frames at a specific frame index.
    CV_WRAP virtual void setMaxPictureSizeB(int32_t frameIdx, int32_t iMaxPictureSize_B) = 0;

    /// Set the chroma QP offsets at a specific frame index.
    CV_WRAP virtual void setQPChromaOffsets(int32_t frameIdx, int32_t iQp1Offset, int32_t iQp2Offset) = 0;

    /// Set whether to use Auto QP at a specific frame index.
    CV_WRAP virtual void setAutoQP(int32_t frameIdx, bool bUseAutoQP) = 0;

    /// Set the HDR index at a specific frame index.
    CV_WRAP virtual void setHDRIndex(int32_t frameIdx, int32_t iHDRIdx) = 0;

    /// Indicate that frameIdx is a skip frame
    CV_WRAP virtual void setIsSkip(int32_t frameIdx) = 0;

    /// Indicate whether SAO (Sample Adaptive Offset) is enabled for the frame
    CV_WRAP virtual void setSAO(int32_t frameIdx, bool bSAOEnabled) = 0;

    /// Set Auto QP threshold QP and delta QP at a specific frame index.
    CV_WRAP virtual void setAutoQPThresholdQPAndDeltaQP(int32_t frameIdx, bool bEnableUserAutoQPValues,
            std::vector<int> thresholdQP, std::vector<int> deltaQP) = 0;

    //
    // static functions
    //

    /// Get supported profiles
    static CV_WRAP String getProfiles(Codec codec);

    /// Get supported levels
    static CV_WRAP String getLevels(Codec codec);

};

/// Factory function to create a decoder instance.
CV_EXPORTS_W Ptr<Decoder> createDecoder(
    const String& filename,         ///< Onput video file name or stream URL
    const DecoderInitParams& params ///< Decoder initialization parameters
);

/// Factory function to create a decoder instance.
CV_EXPORTS_W Ptr<Encoder> createEncoder(
    const String& filename,           ///< Output video file name or stream URL
    const EncoderInitParams& params,  ///< %Encoder initialization parameters
    Ptr<EncoderCallback> callback = 0 ///< Callback (not supported for Python API)
);

//! @}

////////////////////
// IMPLEMENTATION //
////////////////////

//! @cond IGNORED

// Putting the initializer implementation here with prefix _ to the parameters will allow the
// interface to have the same parameter names as the struct member names; this maps nicely to python
// wrapper. Not doing this there would lead to a lot of Wshadow warnings.
inline DecoderInitParams::DecoderInitParams(Codec _codec, int _fourcc, int _fourccConvert,
                                            int _maxFrames, BitDepth _bitDepth)
    : codec(_codec), fourcc(_fourcc), fourccConvert(_fourccConvert), maxFrames(_maxFrames),
      bitDepth(_bitDepth), szReturnQueue(0) {}

inline RCSettings::RCSettings(RCMode _mode, Entropy _entropy, int _bitrate, int _maxBitrate,
    int _cpbSize, int _initialDelay, bool _fillerData, int _maxQualityTarget,
    int _maxPictureSizeI, int _maxPictureSizeP, int _maxPictureSizeB, bool _skipFrame, int _maxSkip)
    : mode(_mode), entropy(_entropy), bitrate(_bitrate), maxBitrate(_maxBitrate), cpbSize(_cpbSize),
      initialDelay(_initialDelay), fillerData(_fillerData), maxQualityTarget(_maxQualityTarget),
      maxPictureSizeI(_maxPictureSizeI), maxPictureSizeP(_maxPictureSizeP),
      maxPictureSizeB(_maxPictureSizeB), skipFrame(_skipFrame), maxSkip(_maxSkip) {}

inline GOPSettings::GOPSettings(GOPMode _mode, GDRMode _gdrMode, int _gopLength, int _nrBFrames,
                                bool _longTermRef, int _longTermFreq, int _periodIDR)
    : mode(_mode), gdrMode(_gdrMode), gopLength(_gopLength), nrBFrames(_nrBFrames),
      longTermRef(_longTermRef), longTermFreq(_longTermFreq), periodIDR(_periodIDR) {}

inline ProfileSettings::ProfileSettings(String _profile, String _level, Tier _tier)
    : profile(_profile), level(_level), tier(_tier) {}

inline GlobalMotionVector::GlobalMotionVector(int _frameIndex, int _gmVectorX, int _gmVectorY)
    : frameIndex(_frameIndex), gmVectorX(_gmVectorX), gmVectorY(_gmVectorY) {}

inline EncoderInitParams::EncoderInitParams(Codec _codec, int _fourcc, RCMode _rcMode, int _bitrate,
    int _pictWidth, int _pictHeight, int _frameRate, int _gopLength, int _nrBFrames)
    : codec(_codec), fourcc(_fourcc), rcMode(_rcMode), bitrate(_bitrate), pictWidth(_pictWidth),
      pictHeight(_pictHeight), frameRate(_frameRate), gopLength(_gopLength), nrBFrames(_nrBFrames) {}


//! @endcond

}  // namespace vcucodec
}  // namespace cv

#endif // OPENCV_VCUCODEC_HPP
