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
    : filename_(filename), params_(params)
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

    pDecConfig->tOutputFourCC = FOURCC(NV12);
    std::cout << "VCU2 Decoder: Using FourCC " << pDecConfig->tOutputFourCC << std::endl;
    std::cout << "VCU2 Decoder: Using fourcc " << params.fourcc << std::endl;

    CtrlswDecOpen(pDecConfig, decodeCtx_, wCfg);
    initialized_ = decodeCtx_ != nullptr;
    if (!initialized_) {
        CV_LOG_ERROR(NULL, "Failed to initialize VCU2 decoder");
        throw std::runtime_error("VCU2 decoder initialization failed");
    }
}

VCUDecoder::~VCUDecoder() {
    CV_LOG_DEBUG(NULL, "VCUDecoder destructor called");
    cleanup();
}

// Implement the pure virtual function from base class
bool VCUDecoder::nextFrame(OutputArray frame, RawInfo& frame_info) /* override */
{
    AL_TBuffer* pFrame = nullptr;

    if (!vcu2_available_ || !initialized_) {
        CV_LOG_DEBUG(NULL, "VCU2 not available or not initialized");
        return false;
    }

    if(!decodeCtx_)
        return false;

    if(!decodeCtx_->running) {
        decodeCtx_->StartRunning(wCfg);
    }

    CV_LOG_DEBUG(NULL, "VCU2 nextFrame called (placeholder implementation)");
    if(decodeCtx_->eos ) {
        std::cout << "eos before GetFrameFromQ" << std::endl;
        constexpr bool no_wait = false;
        pFrame = decodeCtx_->GetFrameFromQ(no_wait);
    } else {
        pFrame = decodeCtx_->GetFrameFromQ();
    }

    if(pFrame) {
        retrieveVideoFrame(frame, pFrame, frame_info);
        frame_info.eos = false;
    } else  {
        std::cout << "GetFrameFromQ is nullptr" << std::endl;
        if(decodeCtx_->eos ) {
            std::cout << "eos after GetFrameFromQ" << std::endl;
            frame_info.eos = true;
            return false;
        }
    }

    return true;
}

bool VCUDecoder::set(int propId, double value)
{
    return false;
}

double VCUDecoder::get(int propId) const {
    double result = 0.0;
    return result; // Placeholder implementation
}


void VCUDecoder::cleanup() {
    if (vcu2_available_ && initialized_) {
        // TODO: Cleanup VCU2 resources
        CV_LOG_DEBUG(NULL, "VCU2 decoder cleanup");
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

        size_t stepY = frame_info.width;
        CV_CheckGE(stepY, (size_t)frame_info.width, "");
        size_t stepUV = frame_info.width;
        CV_CheckGE(stepUV, (size_t)frame_info.width, "");

        dst.create(Size(frame_info.width, frame_info.height * 3 / 2), CV_8UC1);
        Mat dst_ = dst.getMat();
        Mat srcY(sz, CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), stepY);
        Mat srcUV(Size(frame_info.width, frame_info.height / 2), CV_8UC1,
            AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), stepUV);
        srcY.copyTo(dst_(Rect(0, 0, frame_info.width, frame_info.height)));
        srcUV.copyTo(dst_(Rect(0, frame_info.height, frame_info.width,
                               frame_info.height / 2)));
        break;
    }
    } // end switch
    AL_Buffer_Destroy(pFrame);
}


} // namespace vcucodec
} // namespace cv
