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

extern "C" {
#include "config.h"
#include "lib_decode/lib_decode.h"
}

#include "opencv2/core/utils/logger.hpp"
#include "private/vcuutils.hpp"

namespace cv {
namespace vcucodec {

VCUDecoder::VCUDecoder(const String& filename, const DecoderInitParams& params)
    : filename_(filename), params_(params), rawOutput_(RawOutput::create())
{
    // VCU2 initialization will be implemented when VCU2 Control Software is available
    CV_LOG_INFO(NULL, "VCU2 Decoder initialized");
    vcu2_available_ = true;

    std::shared_ptr<Config> pDecConfig = std::shared_ptr<Config>(new Config());
    pDecConfig->sIn = (std::string)filename;

    switch(params_.codecType) {
    case VCU_AVC:  pDecConfig->tDecSettings.eCodec = AL_CODEC_AVC;  break;
    case VCU_HEVC: pDecConfig->tDecSettings.eCodec = AL_CODEC_HEVC; break;
    case VCU_JPEG: pDecConfig->tDecSettings.eCodec = AL_CODEC_JPEG; break;
    default: CV_Error(cv::Error::StsBadArg, "Unsupported codec type");
    }

    pDecConfig->tOutputFourCC = params.fourcc;

    if (params_.maxFrames > 0)
        pDecConfig->iMaxFrames = params_.maxFrames;

    decodeCtx_ = DecContext::create(pDecConfig, rawOutput_, wCfg);
    initialized_ = decodeCtx_ != nullptr;
    if (!initialized_) {
        CV_Error(cv::Error::StsError, "VCU2 decoder initialization failed");
    }
}

VCUDecoder::~VCUDecoder() {
    CV_LOG_DEBUG(NULL, "VCUDecoder destructor called");
    cleanup();
}

// Implement the pure virtual function from base class
bool VCUDecoder::nextFrame(OutputArray frame, RawInfo& frame_info) /* override */
{
    Ptr<Frame> pFrame = nullptr;

    if (!vcu2_available_ || !initialized_) {
        CV_LOG_DEBUG(NULL, "VCU2 not available or not initialized");
        return false;
    }

    if(!decodeCtx_)
        return false;

    if(!decodeCtx_->running()) {
        decodeCtx_->start(wCfg);
    }

    CV_LOG_DEBUG(NULL, "VCU2 nextFrame called (placeholder implementation)");
    if(decodeCtx_->eos()) {
        pFrame = rawOutput_->dequeue(std::chrono::milliseconds::zero());
    } else {
        pFrame = rawOutput_->dequeue(std::chrono::milliseconds(100));
    }

    frame_info.eos = false;
    if(pFrame) {
        retrieveVideoFrame(frame, pFrame->getBuffer(), frame_info);
    } else  {
        if(decodeCtx_->eos()) {
            frame_info.eos = true;
            decodeCtx_->finish();
        }
        return false;
    }

    return true;
}

bool VCUDecoder::set(int propId, double value)
{
    bool result = false;
    if (propId < CV__CAP_PROP_LATEST) {
        result = setCaptureProperty(propId, value);
    }
    return false;
}

double VCUDecoder::get(int propId) const {
    double result = 0.0;
    if (propId < CV__CAP_PROP_LATEST) {
        result = getCaptureProperty(propId);
    }
    return result; // Placeholder implementation
}


void VCUDecoder::cleanup() {
    if (vcu2_available_ && initialized_) {
        decodeCtx_->finish();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        AL_Lib_Decoder_DeInit();
    }
    initialized_ = false;
}

void VCUDecoder::retrieveVideoFrame(OutputArray dst, AL_TBuffer* pFrame, RawInfo& frame_info)
{
    TFourCC fourcc = AL_PixMapBuffer_GetFourCC(pFrame);
    AL_StringFourCC fourcc_str = AL_FourCCToString(fourcc);
    AL_TDimension tYuvDim = AL_PixMapBuffer_GetDimension(pFrame);
    int32_t bitdepth = AL_GetBitDepth(fourcc);
    int32_t stride = AL_PixMapBuffer_GetPlanePitch(pFrame, AL_PLANE_Y);
    frame_info.width = tYuvDim.iWidth;
    frame_info.height = tYuvDim.iHeight;
    frame_info.fourcc = fourcc;
    frame_info.bitDepth = bitdepth;
    frame_info.stride = stride;

    switch(fourcc)
    {
    case FOURCC(Y800):
    {
        Size sz = Size(frame_info.width, frame_info.height);
        size_t step = frame_info.width;
        CV_CheckGE(step, (size_t)frame_info.width, "");
        Mat src(sz, CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), step);
        src.copyTo(dst);
        break;
    }
    case (FOURCC(NV12)):
    {
        Size sz = Size(frame_info.width, frame_info.height);

        size_t stepY = frame_info.stride;
        CV_CheckGE(stepY, (size_t)sz.width, "stride must be bigger than or equal to width");
        size_t stepUV = frame_info.stride;
        CV_CheckGE(stepUV, (size_t)sz.width, "stride must be bigger than or equal to width");

        dst.create(Size(sz.width, sz.height * 3 / 2), CV_8UC1);
        Mat dst_ = dst.getMat();
        Mat srcY(sz, CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), stepY);
        Mat srcUV(Size(sz.width, sz.height / 2), CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), stepUV);
        srcY.copyTo(dst_(Rect(0, 0, sz.width, sz.height)));
        srcUV.copyTo(dst_(Rect(0, sz.height, sz.width, sz.height / 2)));
        break;
    }
    } // end switch
}

bool VCUDecoder::setCaptureProperty(int propId, double value)
{
    bool result;
    switch (propId)
    {
    // unsupported properties:
    case CAP_PROP_POS_MSEC:
    case CAP_PROP_POS_FRAMES:
    case CAP_PROP_POS_AVI_RATIO:
    case CAP_PROP_FRAME_WIDTH:
    case CAP_PROP_FRAME_HEIGHT:
    case CAP_PROP_FOURCC:
    case CAP_PROP_FRAME_COUNT:
    case CAP_PROP_FPS:
    case CAP_PROP_FORMAT:
    case CAP_PROP_MODE:
    case CAP_PROP_BRIGHTNESS:
    case CAP_PROP_CONTRAST:
    case CAP_PROP_SATURATION:
    case CAP_PROP_HUE:
    case CAP_PROP_GAIN:
    case CAP_PROP_EXPOSURE:
    case CAP_PROP_CONVERT_RGB:
    case CAP_PROP_WHITE_BALANCE_BLUE_U:
    case CAP_PROP_RECTIFICATION:
    case CAP_PROP_MONOCHROME:
    case CAP_PROP_SHARPNESS:
    case CAP_PROP_AUTO_EXPOSURE:
    case CAP_PROP_GAMMA:
    case CAP_PROP_TEMPERATURE:
    case CAP_PROP_TRIGGER:
    case CAP_PROP_TRIGGER_DELAY:
    case CAP_PROP_WHITE_BALANCE_RED_V:
    case CAP_PROP_ZOOM:
    case CAP_PROP_FOCUS:
    case CAP_PROP_GUID:
    case CAP_PROP_ISO_SPEED:
    case CAP_PROP_BACKLIGHT:
    case CAP_PROP_PAN:
    case CAP_PROP_TILT:
    case CAP_PROP_ROLL:
    case CAP_PROP_IRIS:
    case CAP_PROP_SETTINGS:
    case CAP_PROP_BUFFERSIZE:
    case CAP_PROP_AUTOFOCUS:
        result = false;
        break;
    }
    return result; // Placeholder implementation
}
double VCUDecoder::getCaptureProperty(int propId) const
{
    double result;
    switch (propId)
    {
    // unsupported properties:
    case CAP_PROP_POS_MSEC:
    case CAP_PROP_POS_FRAMES:
    case CAP_PROP_POS_AVI_RATIO:
    case CAP_PROP_FRAME_WIDTH:
    case CAP_PROP_FRAME_HEIGHT:
    case CAP_PROP_FOURCC:
    case CAP_PROP_FRAME_COUNT:
    case CAP_PROP_FPS:
    case CAP_PROP_FORMAT:
    case CAP_PROP_MODE:
    case CAP_PROP_BRIGHTNESS:
    case CAP_PROP_CONTRAST:
    case CAP_PROP_SATURATION:
    case CAP_PROP_HUE:
    case CAP_PROP_GAIN:
    case CAP_PROP_EXPOSURE:
    case CAP_PROP_CONVERT_RGB:
    case CAP_PROP_WHITE_BALANCE_BLUE_U:
    case CAP_PROP_RECTIFICATION:
    case CAP_PROP_MONOCHROME:
    case CAP_PROP_SHARPNESS:
    case CAP_PROP_AUTO_EXPOSURE:
    case CAP_PROP_GAMMA:
    case CAP_PROP_TEMPERATURE:
    case CAP_PROP_TRIGGER:
    case CAP_PROP_TRIGGER_DELAY:
    case CAP_PROP_WHITE_BALANCE_RED_V:
    case CAP_PROP_ZOOM:
    case CAP_PROP_FOCUS:
    case CAP_PROP_GUID:
    case CAP_PROP_ISO_SPEED:
    case CAP_PROP_BACKLIGHT:
    case CAP_PROP_PAN:
    case CAP_PROP_TILT:
    case CAP_PROP_ROLL:
    case CAP_PROP_IRIS:
    case CAP_PROP_SETTINGS:
    case CAP_PROP_BUFFERSIZE:
    case CAP_PROP_AUTOFOCUS:
       result = 0.0; // Placeholder for properties that are not implemented
       break;
    }
    return result;
}


} // namespace vcucodec
} // namespace cv
