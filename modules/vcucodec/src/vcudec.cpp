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
#include "vcudec.hpp"
#include "vcuvideoframe.hpp"

extern "C" {
#include "config.h"
#include "lib_common/PicFormat.h"
#include "lib_common/PixMapBuffer.h"

#include "lib_decode/lib_decode.h"
}

#include "opencv2/core/utils/logger.hpp"
#include "vcuutils.hpp"


#include <thread>
namespace cv {
namespace vcucodec {
namespace { // anonymous

/// Build Mat headers wrapping the HW buffer planes for a given fourcc/frame.
/// These Mats do NOT own the data — they point directly into the CMA buffer.
std::vector<Mat> buildSrcPlanes(AL_TBuffer* pFrame, const RawInfo& info)
{
    std::vector<Mat> planes;
    switch(info.fourcc)
    {
    case FOURCC(Y800):
    {
        Size sz(info.width, info.height);
        planes.push_back(Mat(sz, CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), (size_t)info.stride));
        break;
    }
    case FOURCC(Y010):
    case FOURCC(Y012):
    {
        Size sz(info.width, info.height);
        planes.push_back(Mat(sz, CV_16UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), (size_t)info.stride));
        break;
    }
    case FOURCC(NV12):
    {
        Size szY(info.width, info.height);
        Size szUV(info.width / 2, info.height / 2);
        planes.push_back(Mat(szY,  CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y),  (size_t)info.stride));
        planes.push_back(Mat(szUV, CV_8UC2,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), (size_t)info.stride));
        break;
    }
    case FOURCC(I420):
    {
        Size szY(info.width, info.height);
        Size szUV(info.width / 2, info.height / 2);
        size_t stepUV = info.stride / 2;
        planes.push_back(Mat(szY,  CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), (size_t)info.stride));
        planes.push_back(Mat(szUV, CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_U), stepUV));
        planes.push_back(Mat(szUV, CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_V), stepUV));
        break;
    }
    case FOURCC(P010):
    case FOURCC(P012):
    {
        Size szY(info.width, info.height);
        Size szUV(info.width / 2, info.height / 2);
        planes.push_back(Mat(szY,  CV_16UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y),  (size_t)info.stride));
        planes.push_back(Mat(szUV, CV_16UC2,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), (size_t)info.stride));
        break;
    }
    case FOURCC(NV16):
    {
        Size szY(info.width, info.height);
        Size szUV(info.width / 2, info.height);
        planes.push_back(Mat(szY,  CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y),  (size_t)info.stride));
        planes.push_back(Mat(szUV, CV_8UC2,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), (size_t)info.stride));
        break;
    }
    case FOURCC(P210):
    case FOURCC(P212):
    {
        Size szY(info.width, info.height);
        Size szUV(info.width / 2, info.height);
        planes.push_back(Mat(szY,  CV_16UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y),  (size_t)info.stride));
        planes.push_back(Mat(szUV, CV_16UC2,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), (size_t)info.stride));
        break;
    }
    case FOURCC(I444):
    {
        Size sz(info.width, info.height);
        planes.push_back(Mat(sz, CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), (size_t)info.stride));
        planes.push_back(Mat(sz, CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_U), (size_t)info.stride));
        planes.push_back(Mat(sz, CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_V), (size_t)info.stride));
        break;
    }
    case FOURCC(I4AL):
    case FOURCC(I4CL):
    {
        Size sz(info.width, info.height);
        planes.push_back(Mat(sz, CV_16UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), (size_t)info.stride));
        planes.push_back(Mat(sz, CV_16UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_U), (size_t)info.stride));
        planes.push_back(Mat(sz, CV_16UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_V), (size_t)info.stride));
        break;
    }
    default:
        CV_Error(Error::StsUnsupportedFormat, "Unsupported pixel format");
    }
    return planes;
}

} // anonymous namespace


VCUDecoder::VCUDecoder(const String& filename, const DecoderInitParams& params,
                       Ptr<DecoderCallback> callback)
    : filename_(filename), params_(params), rawOutput_(RawOutput::create())
{
    if (!validateParams(params))
        return;

    std::shared_ptr<DecContext::Config> pDecConfig
            = std::shared_ptr<DecContext::Config>(new DecContext::Config());
    pDecConfig->sIn = (std::string)filename;
    pDecConfig->iExtraBuffers = std::max(1, params_.extraFrames);
#ifdef HAVE_VCU2_CTRLSW
    pDecConfig->tDecSettings.uNumBuffersHeldByNextComponent = pDecConfig->iExtraBuffers;
#endif

    switch(params_.codec)
    {
    // note pixel FOURCC is returned in property CAP_PROP_CODEC_PIXEL_FORMAT (not CAP_PROP_FOURCC)
    case Codec::AVC:
        pDecConfig->tDecSettings.eCodec = AL_CODEC_AVC;
        setCaptureProperty(CAP_PROP_FOURCC, FOURCC(H264), false);
        break;
    case Codec::HEVC:
        pDecConfig->tDecSettings.eCodec = AL_CODEC_HEVC;
        setCaptureProperty(CAP_PROP_FOURCC, FOURCC(HEVC), false);
        break;
    case Codec::JPEG:
        pDecConfig->tDecSettings.eCodec = AL_CODEC_JPEG;
        setCaptureProperty(CAP_PROP_FOURCC, FOURCC(MJPG), false);
        break;
    default:
        CV_Error(cv::Error::StsBadArg, "Unsupported codec type");
    }

    if (params.fourcc == 0 || params.fourcc == FOURCC(AUTO))
    {
        pDecConfig->tOutputFourCC = FOURCC(NULL);
    }
    else
    {
        pDecConfig->tOutputFourCC = params.fourcc;
    }

    if (params_.maxFrames > 0)
        pDecConfig->iMaxFrames = params_.maxFrames;

    pDecConfig->iOutputBitDepth = static_cast<int>(params_.bitDepth);

    pDecConfig->decoderCallback = callback;

    // Set frame rate from init params (used when stream doesn't contain timing info)
    pDecConfig->tDecSettings.uFrameRate = params_.fpsNum;
    pDecConfig->tDecSettings.uClkRatio = params_.fpsDen;
    pDecConfig->tDecSettings.bForceFrameRate = params_.forceFps;

    decodeCtx_ = DecContext::create(pDecConfig, rawOutput_, wCfg);
    initialized_ = decodeCtx_ != nullptr;
    if (!initialized_)
    {
        CV_Error(cv::Error::StsError, "VCU2 decoder initialization failed");
    }
    setCaptureProperty(CAP_PROP_FPS, (double)params_.fpsNum / (double)params_.fpsDen, false);
    updateFramePosition();
}

VCUDecoder::~VCUDecoder()
{
    CV_LOG_DEBUG(NULL, "VCUDecoder destructor called");
    cleanup();
}

bool VCUDecoder::validateParams(const DecoderInitParams& params)
{
    bool valid = params.codec == Codec::HEVC || params.codec == Codec::AVC;
#ifdef HAVE_VCU2_CTRLSW
    valid |= params.codec == Codec::JPEG;
#endif
    if (!valid)
    {
        CV_Error(cv::Error::StsBadArg, "Unsupported codec type");
        return false;
    }
    auto fi = FormatInfo(params.fourcc);
    valid = fi.decodeable;
    if (!valid)
    {
        CV_Error(cv::Error::StsBadArg, "Unsupported output fourcc");
        return false;
    }
    valid = params.bitDepth == BitDepth::FIRST || params.bitDepth == BitDepth::ALLOC ||
            params.bitDepth == BitDepth::STREAM || params.bitDepth == BitDepth::B8 ||
            params.bitDepth == BitDepth::B10 || params.bitDepth == BitDepth::B12;
    if (!valid)
    {
        CV_Error(cv::Error::StsBadArg, "Unsupported bit depth setting");
        return false;
    }
    valid = params.extraFrames >= 0;
    if (!valid) {
        CV_Error(cv::Error::StsBadArg, "extraFrames must be >= 0");
        return false;
    }
    valid = params.maxFrames >= 0;
    if (!valid) {
        CV_Error(cv::Error::StsBadArg, "maxFrames must be >= 0");
        return false;
    }
    return valid;
}

// New single API entry point
DecodeStatus VCUDecoder::nextFrame(Ptr<VideoFrame>& frame) /* override */
{
    if (!initialized_ || !decodeCtx_)
        CV_Error(cv::Error::StsError, "Decoder not initialized");

    if (!decodeCtx_->running() && !decodeCtx_->eos())
        decodeCtx_->start(wCfg);

    Ptr<Frame> pFrame;
    if (decodeCtx_->eos())
        pFrame = rawOutput_->dequeue(std::chrono::milliseconds::zero());
    else
        pFrame = rawOutput_->dequeue(std::chrono::milliseconds(100));

    if (pFrame)
    {
        RawInfo fi;
        pFrame->rawInfo(fi);
        fi.width  -= fi.cropLeft + fi.cropRight;
        fi.height -= fi.cropTop  + fi.cropBottom;
        fi.fourcc  = pFrame->getFourCC();
        updateRawInfo(fi);

        frame = makePtr<VideoFrameImpl>(pFrame, fi,
                                        buildSrcPlanes(pFrame->getBuffer(), fi),
                                        pinRegistry_);
        ++frameIndex_;
        updateFramePosition();
        return DECODE_FRAME;
    }

    frame.reset();
    if (decodeCtx_->eos())
    {
        decodeCtx_->finish();
        return DECODE_EOS;
    }
    return DECODE_TIMEOUT;
}

bool VCUDecoder::set(int propId, double value)
{
    bool result = false;
    if (propId < CV__CAP_PROP_LATEST)
    {
        result = setCaptureProperty(propId, value, true);
    }
    return result;
}

double VCUDecoder::get(int propId) const {
    double result = 0.0;
    if (propId < CV__CAP_PROP_LATEST)
    {
        result = getCaptureProperty(propId);
    }
    return result; // Placeholder implementation
}

String VCUDecoder::streamInfo() const {
    return decodeCtx_ ? decodeCtx_->streamInfo() : String();
}

String VCUDecoder::statistics() const {
    return decodeCtx_ ? decodeCtx_->statistics() : String();
}


void VCUDecoder::cleanup()
{
    if (initialized_)
    {
        decodeCtx_->finish();
        // Drain any frames that were enqueued into the raw-output queue
        // between finish()'s initial flush and the worker thread join.
        // These frames hold AL_Buffer refs that must be released before
        // AL_Decoder_Destroy, otherwise its pool cleanup asserts.
        rawOutput_->flush();
        pinRegistry_->revokeAll();
        decodeCtx_->destroyDecoder();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        AL_Lib_Decoder_DeInit();
    }
    initialized_ = false;
}

// Not available from the Allegro decoder SDK:
// CAP_PROP_SAR_NUM/DEN: SAR is in AL_TVuiParam (SPS), not exposed by decoder public API.
// CAP_PROP_BITRATE: No bitrate accessor; would need manual byte accumulation.
// CAP_PROP_FRAME_TYPE: AL_TPictureDecMetaData has no eType field (encoder-only).


void VCUDecoder::updateRawInfo(const RawInfo& frame_info)
{
    bool changed = !rawInfoInitialized_ || frame_info != rawInfo_;
    if (changed)
    {
        std::lock_guard<std::mutex> lock(rawInfoMutex_);
        if (!rawInfoInitialized_ || rawInfo_.fourcc != frame_info.fourcc)
        {
            setCaptureProperty(CAP_PROP_CODEC_PIXEL_FORMAT, (double)frame_info.fourcc, false);
        }
        if (!rawInfoInitialized_ || rawInfo_.width != frame_info.width) {
            setCaptureProperty(CAP_PROP_FRAME_WIDTH, (double)frame_info.width, false);
        }
        if (!rawInfoInitialized_ || rawInfo_.height != frame_info.height) {
            setCaptureProperty(CAP_PROP_FRAME_HEIGHT, (double)frame_info.height, false);
        }
        rawInfo_ = frame_info;
        rawInfoInitialized_ = true;
    }
}

void VCUDecoder::updateFramePosition()
{
    double fps = (double)params_.fpsNum / (double)params_.fpsDen;
    setCaptureProperty(CAP_PROP_POS_FRAMES, (double)frameIndex_, false);
    setCaptureProperty(CAP_PROP_POS_MSEC, (fps > 0) ? frameIndex_ * 1000.0 / fps : 0.0, false);
}

bool VCUDecoder::setCaptureProperty(int propId, double value, bool external)
{
    std::lock_guard<std::mutex> lock(capturePropertiesMutex_);

    bool result = true;

    if (!external)
    {
        captureProperties_[propId] = value;
    }
    else
    {
        switch (propId)
        {
            // supported to be set by external client:

        case CAP_PROP_FPS:
            captureProperties_[propId] = value;
            break;

        default:
            result = false;
            break;
        }
    }
    return result;
}

double VCUDecoder::getCaptureProperty(int propId) const
{
    double result = 0.0;
    std::lock_guard<std::mutex> lock(capturePropertiesMutex_);
    auto it = captureProperties_.find(propId);
    if (it != captureProperties_.end())
    {
        result = it->second;
    }
    return result;
}

// static functions:

/*static*/ String Decoder::getFourCCs()
{
    return FormatInfo::getFourCCs(true);
}

} // namespace vcucodec
} // namespace cv
