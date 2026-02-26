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
    (Video Codec Unit). Support for decoding by VDU (Video Decode Unit) will be added in a future
    release.

    The VCU codec module includes the following main classes:
    - cv::vcucodec::Decoder : Interface for decoding video streams.
      Use cv::vcucodec::createDecoder to create an instance.
    - cv::vcucodec::Encoder : Interface for encoding video streams.
      Use cv::vcucodec::createEncoder to create an instance.

    The doc/ folder contains .dox files describing decoder and encoder and are inlined in the
    Decoder and Encoder class definitions when generating Doxygen documentation.

    Current limitations:
    - Support for VDU (Video Decode Unit) hardware to be added in a future release.
    - No tiled format support on VCU2 devices.

  @}
**/

namespace cv {
namespace vcucodec {

//! @addtogroup vcucodec
//! @{

/// Struct RawInfo defines a raw YUV frame containing metadata such as format, dimensions, and stride.
struct CV_EXPORTS_W_SIMPLE RawInfo {
    CV_PROP_RW int  fourcc;         ///< Output format as FOURCC code.
    CV_PROP_RW int  bitsPerLuma;    ///< Bit depth of the output data, 8, 10, or 12 bits per channel.
    CV_PROP_RW int  bitsPerChroma;  ///< Bit depth of the output data, 8, 10, or 12 bits per channel.
    CV_PROP_RW int  stride;         ///< Stride of the output frame in bytes.
    CV_PROP_RW int  strideChroma;   ///< Stride of Chroma buffer of the output frame in bytes.
    CV_PROP_RW int  width;          ///< Width of the raw frame.
    CV_PROP_RW int  height;         ///< Height of the raw frame.
    CV_PROP_RW int  posX;           ///< Position x offset.
    CV_PROP_RW int  posY;           ///< Position y offset.
    CV_PROP_RW int  cropTop;        ///< Crop top offset.
    CV_PROP_RW int  cropBottom;     ///< Crop bottom offset.
    CV_PROP_RW int  cropLeft;       ///< Crop left offset.
    CV_PROP_RW int  cropRight;      ///< Crop right offset.

    CV_PROP_RW PicStruct picStruct; ///< Picture structure (frame, top/bottom field, ...).

};

/// @brief Initialization parameters for the decoder.
///
/// Passed to @ref cv::vcucodec::createDecoder "createDecoder()" to configure the codec type,
/// output pixel format, bit depth, frame rate, and decode limits. The pixel format can be set
/// explicitly via fourcc or left as VCU_FOURCC_AUTO for automatic selection based on the
/// stream. See @ref dec_fourcc_table "Supported output FOURCC codes" for accepted values.
struct CV_EXPORTS_W_SIMPLE DecoderInitParams
{
    CV_PROP_RW Codec codec;       ///< Codec type (AVC, HEVC, JPEG).
    CV_PROP_RW int fourcc;        ///< Format of the output raw data as FOURCC code.
                                  ///< Default is VCU_FOURCC_AUTO (determined automatically).
                                  ///< See @ref dec_fourcc_table "Supported output FOURCC codes".
    CV_PROP_RW int maxFrames;     ///< Maximum number of frames to decode, 0 for unlimited.
    CV_PROP_RW BitDepth bitDepth; ///< Specify output bit depth (first, alloc, stream, 8, 10, 12).
    CV_PROP_RW int extraFrames;   ///< Number of extra frame buffers to allocate for processing,
                                  ///< including the ones held at display side.
    CV_PROP_RW int fpsNum;        ///< Frame rate numerator (default 60000). FPS = fpsNum / fpsDen.
                                  ///< The decoder hardware has limited throughput per core. The
                                  ///< scheduler uses frame rate to allocate enough cores to sustain
                                  ///< real-time decoding, reject decode requests if insufficient
                                  ///< resources are available, and time-share cores between
                                  ///< multiple decode requests.
    CV_PROP_RW int fpsDen;        ///< Frame rate denominator (default 1000). FPS = fpsNum / fpsDen.
    CV_PROP_RW bool forceFps;     ///< Force use of fpsNum/fpsDen instead of stream timing info.
                                  ///< Default: false.

    /// Constructor to initialize decoder parameters with default values.
    CV_WRAP DecoderInitParams(Codec codec = Codec::HEVC, int fourcc = VCU_FOURCC_AUTO,
        int maxFrames = 0, BitDepth bitDepth = BitDepth::ALLOC,
        int fpsNum = 60000, int fpsDen = 1000, bool forceFps = false);
};

/// @brief Status returned by Decoder::nextFrame().
enum DecodeStatus {
    DECODE_FRAME   = 0, ///< A frame was successfully decoded and returned.
    DECODE_TIMEOUT = 1, ///< No frame is available yet; call nextFrame() again.
    DECODE_EOS     = 2  ///< End of stream reached; no more frames will be produced.
};

/// @brief Decoded video frame providing access to YUV plane data.
///
/// Returned by Decoder::nextFrame(). The underlying hardware buffer is kept alive
/// as long as a reference to this VideoFrame (or any zero-copy numpy view obtained from it)
/// exists. Holding frames too long may stall the decoder pipeline.
class CV_EXPORTS_W VideoFrame
{
public:
    virtual ~VideoFrame() {}

    /// @brief Get frame metadata (format, dimensions, stride, crop offsets).
    CV_WRAP virtual const RawInfo& info() const = 0;

    /// @brief Get all YUV planes as Mat headers sharing the hardware buffer.
    ///
    /// In C++ the returned Mats share the underlying hardware memory (zero-copy).
    /// Planes are ordered: index 0 = Y, index 1 = UV (semi-planar) or U (planar),
    /// index 2 = V (planar only).  Use copyToVec() when you need independent deep copies.
    ///
    /// @note **Python zero-copy:** The OpenCV Python binding performs a deep copy when
    /// converting Mat to numpy.  To get true zero-copy access in Python, use
    /// @ref cv2.vcucodec.plane_numpy "cv2.vcucodec.plane_numpy(frame, index)" instead,
    /// which returns a numpy array backed directly by the hardware DMA buffer.
    /// The buffer stays pinned as long as the numpy array (or any view of it) is alive.
    /// Example:
    /// @code{.py}
    ///     status, frame = dec.nextFrame()
    ///     y  = cv2.vcucodec.plane_numpy(frame, 0)   # zero-copy Y plane
    ///     uv = cv2.vcucodec.plane_numpy(frame, 1)   # zero-copy UV plane
    /// @endcode
    CV_WRAP virtual std::vector<Mat> planes() const = 0;

    /// @brief Copy all planes into a vector (deep copy).
    CV_WRAP virtual void copyToVec(CV_OUT std::vector<Mat>& planes) const = 0;

    /// @brief Copy all planes into a single contiguous Mat.
    ///
    /// Produces a single-channel byte buffer (CV_8UC1 for 8-bit formats, or
    /// CV_8UC1 with 2× width for 10/12-bit) containing all YUV planes stacked
    /// vertically.  Each plane occupies its full height in the output; chroma
    /// rows narrower than the Y row pitch are zero-padded.
    ///
    /// @param dst   Output Mat — reallocated if size/type do not match.
    /// @param stride Row pitch in bytes for the Y plane.  Chroma pitch is
    ///              derived from the format (equal to Y pitch for semi-planar
    ///              and 4:4:4; half of Y pitch for planar 4:2:0 / 4:2:2
    ///              chroma planes).  When 0 (default), tight packing is used:
    ///              Y pitch = width × bytesPerPixel, no padding.
    ///              Pass `info().stride` to match the hardware buffer stride.
    CV_WRAP virtual void copyTo(CV_OUT Mat& dst, int stride = 0) const = 0;

    /// @brief Convert the frame to the specified color format and store in dst.
    ///
    /// Performs a software color conversion from the native YUV format to the
    /// target format specified by @p fourCC.  Currently supported targets:
    /// - `VideoWriter::fourcc('B','G','R',' ')` — 3-channel BGR (CV_8UC3)
    /// - `VideoWriter::fourcc('B','G','R','A')` — 4-channel BGRA (CV_8UC4)
    ///
    /// @param dst    Output Mat — reallocated if size/type do not match.
    /// @param fourCC Target pixel format as a FOURCC code.
    CV_WRAP virtual void convertTo(CV_OUT Mat& dst, int fourCC) const = 0;
};


// see decoder.dox for documentation of Decoder class

/// @brief Class Decoder is the interface for decoding video streams.
/// This interface provides methods to decode video frames from a stream.
/// See: @ref dec_python_examples_anchor "Decoder Python Examples"
class CV_EXPORTS_W Decoder
{
public:
    /// Virtual destructor for the Decoder interface.
    virtual ~Decoder() {}

    /// @brief Decode the next frame from the stream.
    /// @return DECODE_FRAME if a frame was decoded, DECODE_TIMEOUT if no frame is available yet,
    ///         or DECODE_EOS when the stream has ended.
    CV_WRAP virtual DecodeStatus nextFrame(
        CV_OUT Ptr<VideoFrame>& frame ///< Output: the decoded video frame.
    ) = 0;


    /// Set a property for the decoder.
    /// @return true if the property was set successfully, false otherwise.
    CV_WRAP virtual bool set(
        int propId,  ///< Property identifier.
        double value ///< Value to set for the property.
    ) = 0;

    /// @brief Get the value of a property.
    /// Provided properties:
    /// - CAP_PROP_FOURCC: The codec type (H264, HEVC, MJPG).
    /// - CAP_PROP_CODEC_PIXEL_FORMAT: The pixel format of the decoded frames (NV12, ...).
    /// - CAP_PROP_FRAME_WIDTH: Width of the decoded frames.
    /// - CAP_PROP_FRAME_HEIGHT: Height of the decoded frames.
    /// - CAP_PROP_POS_FRAMES: Current frame position in the stream.
    /// - CAP_PROP_POS_MSEC: Current position in milliseconds (derived from POS_FRAMES / FPS).
    /// - CAP_PROP_FPS: Frame rate (frames per second). Initialized from DecoderInitParams
    ///   fpsNum/fpsDen. Can be overridden via set().
    CV_WRAP virtual double get(
        int propId ///< Property identifier.
    ) const = 0;

    /// @brief Get the information of the stream that was parsed (if any so far).
    /// Returns a multi-line string containing: resolution, FourCC, profile, level, bit depth,
    /// crop offsets (if any), display resolution, sequence picture mode, and buffer count/size.
    CV_WRAP virtual String streamInfo() const = 0;

    /// @brief Get the statistics of the stream that was decoded (if any).
    /// Returns a string containing: decoding time, frame rate (fps), and concealed frame count.
    CV_WRAP virtual String statistics() const = 0;

    /// Get comma separated list of supported FOURCC codes for decoding.
    static CV_WRAP String getFourCCs();
};

/// @brief Struct PictureEncSettings defines the core picture parameters for encoding.
///
/// These settings specify the codec standard, input pixel format, frame dimensions, and
/// framerate. They are provided at encoder creation via
/// @ref cv::vcucodec::EncoderInitParams::pictureEncSettings "EncoderInitParams::pictureEncSettings"
/// and determine the initial encoding configuration.
///
/// See @ref enc_fourcc_table "Supported input FOURCC codes" for the list of accepted
/// pixel formats.
///
/// A separate PictureEncSettings can also be passed per file when using
/// @ref cv::vcucodec::Encoder::writeFile "writeFile()" to encode with a different resolution
/// (must be &lt;= the initial resolution).
struct CV_EXPORTS_W_SIMPLE PictureEncSettings
{
    CV_PROP_RW Codec codec;     ///< Codec of the picture (HEVC, AVC, JPEG).
    CV_PROP_RW int   fourcc;    ///< FourCC code of the picture.
                                ///< See @ref enc_fourcc_table "Supported input FOURCC codes".
    CV_PROP_RW int   width;     ///< Width of the picture.
    CV_PROP_RW int   height;    ///< Height of the picture.
    CV_PROP_RW int   framerate; ///< Framerate of the picture.

    CV_WRAP PictureEncSettings(Codec codec = Codec::HEVC,
        int fourcc = VideoWriter::fourcc('N', 'V', '1', '2'),
        int width = 1280, int height = 720, int framerate = 30
    );
};

/// @brief Struct RCSettings provides rate control settings for the encoder.
///
/// Rate control governs how the encoder distributes bits across frames to meet a target
/// bitrate while maintaining quality. The mode (CBR, VBR, CAPPED_VBR, etc.) determines the
/// overall strategy, while parameters such as bitrate, maxBitrate, cpbSize, and initialDelay
/// fine-tune the buffering model. Per-frame size limits (maxPictureSizeI/P/B) and frame
/// skipping provide additional control over bitrate peaks.
///
/// These settings can be changed mid-stream via @ref cv::vcucodec::Encoder::set(const RCSettings&)
/// "Encoder::set(rcSettings)" or via the dynamic commands
/// @ref cv::vcucodec::Encoder::setBitRate "setBitRate()" and
/// @ref cv::vcucodec::Encoder::setMaxBitRate "setMaxBitRate()".
struct CV_EXPORTS_W_SIMPLE RCSettings
{
    CV_PROP_RW RCMode  mode;          ///< Rate control mode (default VBR).
    CV_PROP_RW Entropy entropy;       ///< Entropy coding mode (CAVLC or CABAC).
    CV_PROP_RW int  bitrate;          ///< Target bitrate in kbits per second.
    CV_PROP_RW int  maxBitrate;       ///< Maximum bitrate in kbits per second.
    CV_PROP_RW int  cpbSize;          ///< Coded Picture Buffer (CPB) size in milliseconds.
                                      ///< Cannot be smaller than initial-delay. Default: 3000.
    CV_PROP_RW int  initialDelay;     ///< Initial delay in milliseconds. Default: 1500.
    CV_PROP_RW bool fillerData;       ///< Add filler data in CBR mode. Default: true.
    CV_PROP_RW int  maxQualityTarget; ///< 0-20. Maximum quality target for CAPPED_VBR. Default: 14.
    CV_PROP_RW int  maxPictureSizeI;  ///< Maximum picture size in kBytes. Default: 0 (unlimited).
    CV_PROP_RW int  maxPictureSizeP;  ///< Maximum P-frame picture size for CBR/VBR.
    CV_PROP_RW int  maxPictureSizeB;  ///< Maximum B-frame picture size,
                                      ///< max = (bitrate/framerate) * allowed peak margin.
    CV_PROP_RW bool skipFrame;        ///< Skip a frame when the CPB buffer size is exceeded and
                                      ///< replace with skip MBs (or CTBs). Default: false.
    CV_PROP_RW int  maxSkip;          ///< Maximum number of skips in a row. Default: unlimited.

    CV_WRAP RCSettings(RCMode mode = RCMode::VBR, Entropy entropy = Entropy::CABAC,
        int bitrate = 4000, int maxBitrate = 4000, int cpbSize = 3000, int initialDelay = 1500,
        bool fillerData = true, int maxQualityTarget = 14, int maxPictureSizeI = 0,
        int maxPictureSizeP = 0, int maxPictureSizeB = 0, bool skipFrame = false,  int maxSkip = -1);
};

/// Struct GOPSettings specifies the structure of the Group Of Pictures (GOP).
struct CV_EXPORTS_W_SIMPLE GOPSettings
{
    CV_PROP_RW GOPMode mode;      ///< Group of pictures mode.
    CV_PROP_RW GDRMode gdrMode;   ///< Gradual Decoder Refresh scheme used for low delay gop-mode.
    CV_PROP_RW int  gopLength;    ///< Distance between two consecutive I-frames.
                                  ///< Default: 30. Range 0-1000. (0,1 is intra-only).
    CV_PROP_RW int  nrBFrames;    ///< Number of B-frames between two consecutive P-frames. For
                                  ///< basic and pyramidal modes. 0-4 for basic GOP mode.
                                  ///< 3,5, or 7 for pyramidal GOP mode. Default: 0.
    CV_PROP_RW bool longTermRef;  ///< Specify that a long-term reference can be dynamically
                                  ///< inserted. Default: false.
    CV_PROP_RW int  longTermFreq; ///< Specify the periodicity in frames; the distance between two
                                  ///< consecutive long-term reference pictures. Default: 0.
    CV_PROP_RW int  periodIDR;    ///< The number of frames between consecutive Instantaneous
                                  ///< Decoder Refresh (IDR) pictures. This might be rounded to a
                                  ///< multiple of the GOP length.
                                  ///< -1 disables, 0 (default) first frame is IDR.
    CV_WRAP GOPSettings(GOPMode mode = GOPMode::BASIC, GDRMode gdrMode = GDRMode::DISABLE,
        int gopLength = 30, int nrBFrames = 0, bool longTermRef = false,
        int longTermFreq = 0, int periodIDR = 0);
};

/// Struct ProfileSettings specifies the encoder profile, level and tier.
struct CV_EXPORTS_W_SIMPLE ProfileSettings
{
    CV_PROP_RW String profile; ///< Encoder profile (e.g., "main", "high"). Empty = auto-detect from FourCC.
    CV_PROP_RW String level;   ///< Encoder level (e.g., "4.1", "5.0"). Empty = library default.
    CV_PROP_RW Tier   tier;    ///< Encoder tier (e.g., Tier::MAIN, Tier::HIGH).

    CV_WRAP ProfileSettings(String profile = "", String level = "", Tier tier = Tier::MAIN);
};

/// Struct SliceSettings specifies slice configuration for the encoder.
struct CV_EXPORTS_W_SIMPLE SliceSettings
{
    CV_PROP_RW int  numSlices;        ///< Number of slices per frame. Default: 1.
                                      ///< [1-256] for AVC. [1-512] for HEVC.
    CV_PROP_RW bool dependentSlice;   ///< Enable dependent slices (HEVC only). Default: false.
    CV_PROP_RW bool subframeLatency;  ///< Enable subframe latency mode for low-latency streaming.
                                      ///< When enabled, slices are output as soon as encoded.
                                      ///< Default: false.

    CV_WRAP SliceSettings(int numSlices = 1, bool dependentSlice = false, bool subframeLatency = false);
};

/// Struct GlobalMotionVector specifies a global motion vector for a frame.
struct CV_EXPORTS_W_SIMPLE GlobalMotionVector
{
    CV_PROP_RW int frameIndex;   ///< Frame index.
    CV_PROP_RW int gmVectorX;    ///< Global motion vector in X direction.
    CV_PROP_RW int gmVectorY;    ///< Global motion vector in Y direction.

    CV_WRAP GlobalMotionVector(int frameIndex = -1, int gmVectorX = 0, int gmVectorY = 0);
};

/// @brief Initialization parameters for the encoder.
///
/// Passed to @ref cv::vcucodec::createEncoder "createEncoder()" to configure picture settings,
/// rate control, GOP structure, profile/level/tier, slice configuration, and global motion
/// vectors. All sub-structures have sensible defaults and can be customized as needed.
struct CV_EXPORTS_W_SIMPLE EncoderInitParams
{
    CV_PROP_RW PictureEncSettings pictureEncSettings; ///< Picture encoding settings.
    CV_PROP_RW RCSettings         rcSettings;         ///< Rate control settings.
    CV_PROP_RW GOPSettings        gopSettings;        ///< Group of pictures settings.
    CV_PROP_RW ProfileSettings    profileSettings;    ///< Profile, level and tier settings.
    CV_PROP_RW SliceSettings      sliceSettings;      ///< Slice configuration settings.
    CV_PROP_RW GlobalMotionVector globalMotionVector; ///< Global motion vector settings.

    CV_WRAP EncoderInitParams() = default;
};

/// @brief Callback interface for feeding encoded data to the decoder (C++ only).
///
/// Implement this interface and pass it to @ref cv::vcucodec::createDecoder "createDecoder()"
/// to provide encoded bitstream data from a custom source (e.g., network stream, memory buffer)
/// instead of reading from a file. The decoder calls onData() when it needs more input,
/// and the implementation should fill the buffer with encoded bitstream data.
///
/// @note Not available from the Python API.
class CV_EXPORTS_W DecoderCallback
{
public:
    virtual ~DecoderCallback() {}
    /// Called when the decoder needs more encoded data. Write up to @p maxSize bytes of
    /// encoded bitstream into @p buffer and return the number of bytes written.
    /// Return 0 to signal end-of-stream.
    virtual size_t onData(uint8_t* buffer, size_t maxSize) = 0;
    /// Called once when the decoder has finished processing all frames.
    virtual void onFinished() = 0;
};

/// @brief Callback interface for receiving encoded data from the encoder (C++ only).
///
/// Implement this interface and pass it to @ref cv::vcucodec::createEncoder "createEncoder()"
/// to receive encoded NAL units as they are produced by the hardware encoder. This is useful
/// for streaming or custom muxing scenarios where direct access to the encoded bitstream is
/// needed instead of writing to a file.
///
/// @note Not available from the Python API.
class CV_EXPORTS_W EncoderCallback
{
public:
    virtual ~EncoderCallback() {}
    /// Called each time the encoder produces encoded data (one or more NAL units).
    virtual void onEncoded(std::vector<std::string_view>& encodedData) = 0;
    /// Called once when the encoder has finished processing all frames after @ref cv::vcucodec::Encoder::eos "eos()".
    virtual void onFinished() = 0;
};


// see encoder.dox for documentation of Encoder class

/// @brief Class Encoder is the interface for encoding video frames to a stream.
/// This interface provides methods to encode video frames and manage encoding parameters.
/// See: @ref enc_python_examples_anchor "Encoder Python Examples"
class CV_EXPORTS_W Encoder
{
public:
    /// Virtual destructor for the Encoder interface.
    virtual ~Encoder() {}

    /// Encode a video frame; either write() or writeFile() can be used, not both.
    CV_WRAP virtual void write(InputArray frame) = 0;

    /// Get frames from RAW YUV input file and encode them; when numFrames=0, encode until EOF.
    /// Either write() or writeFile() can be used, not both.
    CV_WRAP virtual void writeFile(
        const String& filename, ///< Input RAW YUV file name.
        int startFrame = 0,     ///< Start frame index in the input file.
        int numFrames = 0,      ///< Number of frames to encode, 0 for all frames until EOF.
        Ptr<PictureEncSettings> picSettings = nullptr ///< Optional per-file picture settings
                                                      ///< (resolution must be <= initial).
    ) = 0;

    /// Signal the end of the stream to the encoder and wait until final frame is encoded.
    /// @return true if encoding completed successfully, false if timeout or error occurred.
    CV_WRAP virtual bool eos() = 0;


    /// @brief Get the current settings of the encoder as a human-readable multi-line string.
    /// Returns picture settings (codec, fourcc, resolution, framerate), rate control
    /// (mode, bitrate, CPB), GOP structure, profile/level/tier, slice configuration, and
    /// global motion vector settings.
    CV_WRAP virtual String settings() const = 0;

    /// @brief Get the statistics of the encoding session.
    /// Returns a string containing: number of pictures encoded and average frame rate (fps).
    /// Available after encoding has started.
    CV_WRAP virtual String statistics() const = 0;

    /// @brief Set a property for the encoder.
    ///
    /// Supported properties:
    /// - CAP_PROP_FPS: Framerate of the encoding session.
    /// - CAP_PROP_BITRATE: Target bitrate in kbits per second.
    ///
    /// @note Setting FPS or bitrate via this method updates the cached settings only;
    /// use the dynamic commands (setFrameRate(), setBitRate()) to change them mid-stream.
    /// @return true if the property was set successfully, false otherwise.
    CV_WRAP virtual bool set(
        int propId,  ///< Property identifier.
        double value ///< Value to set for the property.
    ) = 0;

    /// @brief Get the value of a property.
    ///
    /// Supported properties:
    /// - CAP_PROP_FOURCC: The codec type (H264, HEVC, MJPG).
    /// - CAP_PROP_CODEC_PIXEL_FORMAT: Input pixel format FOURCC (e.g. NV12).
    /// - CAP_PROP_FRAME_WIDTH: Width of the picture in pixels.
    /// - CAP_PROP_FRAME_HEIGHT: Height of the picture in pixels.
    /// - CAP_PROP_FPS: Framerate of the encoding session.
    /// - CAP_PROP_BITRATE: Target bitrate in kbits per second.
    /// - CAP_PROP_POS_FRAMES: Number of frames encoded so far.
    /// - CAP_PROP_POS_MSEC: Current position in milliseconds (derived from frame count and FPS).
    ///
    CV_WRAP virtual double get(
        int propId ///< Property identifier.
    ) const = 0;


    /// Set rate control settings.
    CV_WRAP virtual void set(const RCSettings& rcSettings) = 0;
    /// Get rate control settings.
    CV_WRAP virtual void get(RCSettings& rcSettings) const = 0;

    /// Set GOP (Group Of Pictures) settings.
    CV_WRAP virtual void set(const GOPSettings& gopSettings) = 0;
    /// Get GOP (Group Of Pictures) settings.
    CV_WRAP virtual void get(GOPSettings& gopSettings) const = 0;

    /// Set profile, level and tier settings.
    CV_WRAP virtual void set(const ProfileSettings& profileSettings) = 0;
    /// Get profile, level and tier settings.
    CV_WRAP virtual void get(ProfileSettings& profileSettings) const = 0;

    /// Set slice settings.
    CV_WRAP virtual void set(const SliceSettings& sliceSettings) = 0;
    /// Get slice settings.
    CV_WRAP virtual void get(SliceSettings& sliceSettings) const = 0;

    /// Set global motion vector.
    CV_WRAP virtual void set(const GlobalMotionVector& gmVector) = 0;
    /// Get global motion vector.
    CV_WRAP virtual void get(GlobalMotionVector& gmVector) const = 0;

    /// Add HDR SEIs to the encoder. Returns an index that can be used with setHDRIndex().
    CV_WRAP virtual int add(const HDRSEIs& hdrSeis) = 0;

    //
    // Dynamic commands
    //

    /// Dynamically indicate a scene change at frameIdx with lookAhead frames
    CV_WRAP virtual void setSceneChange(int32_t frameIdx, int32_t lookAhead) = 0;

    /// Dynamically indicate that frameIdx is a long-term reference
    CV_WRAP virtual void setIsLongTerm(int32_t frameIdx) = 0;

    /// Dynamically indicate that frameIdx should use a long-term reference
    CV_WRAP virtual void setUseLongTerm(int32_t frameIdx) = 0;

    /// Dynamically restart the GOP at frameIdx (next frame will be an IDR)
    CV_WRAP virtual void restartGop(int32_t frameIdx) = 0;

    /// Dynamically restart the GOP at frameIdx with a recovery point SEI
    CV_WRAP virtual void restartGopRecoveryPoint(int32_t frameIdx) = 0;

    /// Dynamically set the GOP length at frameIdx
    CV_WRAP virtual void setGopLength(int32_t frameIdx, int32_t gopLength) = 0;

    /// Dynamically set the number of B-frames at frameIdx
    CV_WRAP virtual void setNumB(int32_t frameIdx, int32_t numB) = 0;

    /// Dynamically set the frequency of IDR frames at frameIdx
    CV_WRAP virtual void setFreqIDR(int32_t frameIdx, int32_t freqIDR) = 0;

    /// Dynamically set the frame rate at frameIdx
    CV_WRAP virtual void setFrameRate(int32_t frameIdx, int32_t frameRate, int32_t clockRatio) = 0;

    /// Dynamically set the target bitrate at frameIdx
    CV_WRAP virtual void setBitRate(int32_t frameIdx, int32_t bitRate) = 0;

    /// Dynamically set the target and maximum bitrate at frameIdx
    CV_WRAP virtual void setMaxBitRate(int32_t frameIdx, int32_t iTargetBitRate, int32_t iMaxBitRate) = 0;

    /// Dynamically set the QP (Quantization Parameter) at a specific frame index.
    CV_WRAP virtual void setQP(int32_t frameIdx, int32_t qp) = 0;

    /// Dynamically set the QP offset at a specific frame index.
    CV_WRAP virtual void setQPOffset(int32_t frameIdx, int32_t iQpOffset) = 0;

    /// Dynamically set the QP bounds at a specific frame index.
    CV_WRAP virtual void setQPBounds(int32_t frameIdx, int32_t iMinQP, int32_t iMaxQP) = 0;

    /// Dynamically set the QP bounds for I-frames at a specific frame index.
    CV_WRAP virtual void setQPBoundsI(int32_t frameIdx, int32_t iMinQP_I, int32_t iMaxQP_I) = 0;

    /// Dynamically set the QP bounds for P-frames at a specific frame index.
    CV_WRAP virtual void setQPBoundsP(int32_t frameIdx, int32_t iMinQP_P, int32_t iMaxQP_P) = 0;

    /// Dynamically set the QP bounds for B-frames at a specific frame index.
    CV_WRAP virtual void setQPBoundsB(int32_t frameIdx, int32_t iMinQP_B, int32_t iMaxQP_B) = 0;

    /// Dynamically set the QP delta between I and P frames at a specific frame index.
    CV_WRAP virtual void setQPIPDelta(int32_t frameIdx, int32_t iQPDelta) = 0;

    /// Dynamically set the QP delta between P and B frames at a specific frame index.
    CV_WRAP virtual void setQPPBDelta(int32_t frameIdx, int32_t iQPDelta) = 0;

    /// Dynamically set the loop filter mode at a specific frame index.
    CV_WRAP virtual void setLFMode(int32_t frameIdx, int32_t iMode) = 0;

    /// Dynamically set the loop filter beta offset at a specific frame index.
    CV_WRAP virtual void setLFBetaOffset(int32_t frameIdx, int32_t iBetaOffset) = 0;

    /// Dynamically set the loop filter tc offset at a specific frame index.
    CV_WRAP virtual void setLFTcOffset(int32_t frameIdx, int32_t iTcOffset) = 0;

    /// Dynamically set the cost mode at a specific frame index.
    CV_WRAP virtual void setCostMode(int32_t frameIdx, bool bCostMode) = 0;

    /// Dynamically set the maximum picture size at a specific frame index.
    CV_WRAP virtual void setMaxPictureSize(int32_t frameIdx, int32_t iMaxPictureSize) = 0;

    /// Dynamically set the maximum picture size for I-frames at a specific frame index.
    CV_WRAP virtual void setMaxPictureSizeI(int32_t frameIdx, int32_t iMaxPictureSize_I) = 0;

    /// Dynamically set the maximum picture size for P-frames at a specific frame index.
    CV_WRAP virtual void setMaxPictureSizeP(int32_t frameIdx, int32_t iMaxPictureSize_P) = 0;

    /// Dynamically set the maximum picture size for B-frames at a specific frame index.
    CV_WRAP virtual void setMaxPictureSizeB(int32_t frameIdx, int32_t iMaxPictureSize_B) = 0;

    /// Dynamically set the chroma QP offsets at a specific frame index.
    CV_WRAP virtual void setQPChromaOffsets(int32_t frameIdx, int32_t iQp1Offset, int32_t iQp2Offset) = 0;

    /// Dynamically set whether to use Auto QP at a specific frame index.
    CV_WRAP virtual void setAutoQP(int32_t frameIdx, bool bUseAutoQP) = 0;

    /// Dynamically set the HDR SEI index at a specific frame index.
    CV_WRAP virtual void setHDRIndex(int32_t frameIdx, int32_t iHDRIdx) = 0;

    /// Dynamically indicate that frameIdx is a skip frame
    CV_WRAP virtual void setIsSkip(int32_t frameIdx) = 0;

    /// Dynamically set whether SAO (Sample Adaptive Offset) is enabled for the frame
    CV_WRAP virtual void setSAO(int32_t frameIdx, bool bSAOEnabled) = 0;

    /// Dynamically set Auto QP threshold QP and delta QP at a specific frame index.
    CV_WRAP virtual void setAutoQPThresholdQPAndDeltaQP(int32_t frameIdx, bool bEnableUserAutoQPValues,
            std::vector<int> thresholdQP, std::vector<int> deltaQP) = 0;

    //
    // static functions
    //

    /// @brief Get a comma-separated list of supported encoder profiles for the given codec.
    ///
    /// The returned profile names can be used in
    /// @ref cv::vcucodec::ProfileSettings::profile "ProfileSettings::profile" when configuring
    /// an encoder via @ref cv::vcucodec::EncoderInitParams "EncoderInitParams".
    ///
    /// For HEVC, profiles include "main", "main-10", "main-422-10", etc.
    /// For AVC, profiles include "baseline", "main", "high", "high-10", etc.
    /// For JPEG, returns "JPEG" (JPEG does not define profiles).
    ///
    /// Example:
    /// @code{.py}
    ///     profiles = cv2.vcucodec.Encoder.getProfiles(cv2.vcucodec.Codec_HEVC)
    ///     print(profiles)  # "main,main-10,main-12,..."
    /// @endcode
    static CV_WRAP String getProfiles(Codec codec);

    /// @brief Get a comma-separated list of supported encoder levels for the given codec.
    ///
    /// The returned level strings can be used in
    /// @ref cv::vcucodec::ProfileSettings::level "ProfileSettings::level" when configuring
    /// an encoder via @ref cv::vcucodec::EncoderInitParams "EncoderInitParams".
    ///
    /// For HEVC, levels range from "1.0" to "6.2".
    /// For AVC, levels range from "0.9" to "6.2".
    /// For JPEG, returns an empty string (JPEG does not define levels).
    ///
    /// Example:
    /// @code{.py}
    ///     levels = cv2.vcucodec.Encoder.getLevels(cv2.vcucodec.Codec_AVC)
    ///     print(levels)  # "0.9,1.0,1.1,...,6.2"
    /// @endcode
    static CV_WRAP String getLevels(Codec codec);
};

/// @brief Create a decoder instance for the given input file or stream.
///
/// Opens the input and initializes the VCU decoder hardware with the specified parameters.
/// The returned @ref cv::vcucodec::Decoder "Decoder" can then be used to decode frames
/// via nextFrame().
CV_EXPORTS_W Ptr<Decoder> createDecoder(
    const String& filename,             ///< Input video file name (ignored when @p callback is provided).
    const DecoderInitParams& params,    ///< Decoder initialization parameters.
    Ptr<DecoderCallback> callback = 0   ///< Optional callback providing encoded bitstream data (C++ only).
                                        ///< When provided, encoded data is read via
                                        ///< @ref cv::vcucodec::DecoderCallback::onData "onData()"
                                        ///< instead of from the file. Not available from Python.
);

/// @brief Create an encoder instance for the given output file or stream.
///
/// Opens the output and initializes the VCU encoder hardware with the specified parameters.
/// The returned @ref cv::vcucodec::Encoder "Encoder" can then be fed frames via write()
/// or writeFile(), and finalized with eos().
///
/// Example:
/// @code{.py}
///     params = cv2.vcucodec.EncoderInitParams()
///     params.pictureEncSettings.codec = cv2.vcucodec.Codec_HEVC
///     params.pictureEncSettings.width = 1920
///     params.pictureEncSettings.height = 1080
///     encoder = cv2.vcucodec.createEncoder("output.h265", params)
/// @endcode
CV_EXPORTS_W Ptr<Encoder> createEncoder(
    const String& filename,           ///< Output video file name or stream URL.
    const EncoderInitParams& params,  ///< Encoder initialization parameters.
    Ptr<EncoderCallback> callback = 0 ///< Optional callback for receiving encoded data (C++ only).
                                      ///< When provided, @ref cv::vcucodec::EncoderCallback::onEncoded
                                      ///< "onEncoded()" is called with each encoded NAL unit, and
                                      ///< @ref cv::vcucodec::EncoderCallback::onFinished "onFinished()"
                                      ///< is called when encoding completes. Not available from Python.
);

//! @}

////////////////////
// IMPLEMENTATION //
////////////////////

//! @cond IGNORED

// Putting the initializer implementation here with prefix _ to the parameters will allow the
// interface to have the same parameter names as the struct member names; this maps nicely to python
// wrapper. Not doing this there would lead to a lot of Wshadow warnings.
inline DecoderInitParams::DecoderInitParams(Codec _codec, int _fourcc,
                                            int _maxFrames, BitDepth _bitDepth,
                                            int _fpsNum, int _fpsDen,
                                            bool _forceFps)
    : codec(_codec), fourcc(_fourcc), maxFrames(_maxFrames),
      bitDepth(_bitDepth), extraFrames(0), fpsNum(_fpsNum), fpsDen(_fpsDen),
      forceFps(_forceFps) {}

inline PictureEncSettings::PictureEncSettings(Codec _codec, int _fourcc, int _width, int _height,
                                              int _framerate)
    : codec(_codec), fourcc(_fourcc), width(_width), height(_height), framerate(_framerate) {}

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

inline SliceSettings::SliceSettings(int _numSlices, bool _dependentSlice, bool _subframeLatency)
    : numSlices(_numSlices), dependentSlice(_dependentSlice), subframeLatency(_subframeLatency) {}

inline GlobalMotionVector::GlobalMotionVector(int _frameIndex, int _gmVectorX, int _gmVectorY)
    : frameIndex(_frameIndex), gmVectorX(_gmVectorX), gmVectorY(_gmVectorY) {}

//! @endcond

}  // namespace vcucodec
}  // namespace cv

#endif // OPENCV_VCUCODEC_HPP
