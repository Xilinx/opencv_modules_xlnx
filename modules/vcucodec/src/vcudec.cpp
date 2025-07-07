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

extern "C" {
#include "config.h"
#include "lib_decode/lib_decode.h"
}

#include "vcudec.hpp"

#include "opencv2/core/utils/logger.hpp"
#include "private/vcuutils.hpp"

namespace cv {
namespace vcucodec {

VCUDecoder::VCUDecoder(const String& filename, const DecoderInitParams& params)
    : filename_(filename), params_(params) {
    // VCU2 initialization will be implemented when VCU2 Control Software is available
    CV_LOG_INFO(NULL, "VCU2 Decoder initialized");
    vcu2_available_ = true;
    // TODO: Initialize VCU2 decoder with actual VCU2 API calls
    // This is a placeholder implementation
    CtrlswDecOpen((std::string)filename, pDecodeCtx, wCfg);
    if(pDecodeCtx != nullptr)
        initialized_ = true;
}

VCUDecoder::~VCUDecoder() {
    CV_LOG_DEBUG(NULL, "VCUDecoder destructor called");
    cleanup();
}

void retrieveVideoFrame(OutputArray dst, AL_TBuffer* pFrame, RawInfo& frame_info)
{
    if (AL_PixMapBuffer_GetFourCC(pFrame) == FOURCC(Y800))
    {
        AL_TDimension tYuvDim = AL_PixMapBuffer_GetDimension(pFrame);
        int frame_width = tYuvDim.iWidth;
        int frame_height = tYuvDim.iHeight;
        Size sz = Size(frame_width, frame_height);

        size_t step = frame_width;
        CV_CheckGE(step, (size_t)frame_width, "");
        Mat src(sz, CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), step);
        src.copyTo(dst);
        frame_info.width = frame_width;
        frame_info.height = frame_height;
    }
    else if (AL_PixMapBuffer_GetFourCC(pFrame) == FOURCC(NV12))
    {
        AL_TDimension tYuvDim = AL_PixMapBuffer_GetDimension(pFrame);
        int frame_width = tYuvDim.iWidth;
        int frame_height = tYuvDim.iHeight;
        Size sz = Size(frame_width, frame_height);

        size_t stepY = frame_width;
        CV_CheckGE(stepY, (size_t)frame_width, "");
        size_t stepUV = frame_width;
        CV_CheckGE(stepUV, (size_t)frame_width, "");

        dst.create(Size(frame_width, frame_height * 3 / 2), CV_8UC1);
        Mat dst_ = dst.getMat();
        Mat srcY(sz, CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), stepY);
        Mat srcUV(Size(frame_width, frame_height / 2), CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), stepUV);
        srcY.copyTo(dst_(Rect(0, 0, frame_width, frame_height)));
        srcUV.copyTo(dst_(Rect(0, frame_height, frame_width, frame_height / 2)));
        frame_info.width = frame_width;
        frame_info.height = frame_height;
    }
    
    AL_Buffer_Destroy(pFrame);
}

// Implement the pure virtual function from base class
bool VCUDecoder::nextFrame(OutputArray frame, RawInfo& frame_info) /* override */
{
    AL_TBuffer* pFrame = nullptr;

    if (!vcu2_available_ || !initialized_) {
        CV_LOG_DEBUG(NULL, "VCU2 not available or not initialized");
        return false;
    }
    
    if(!pDecodeCtx)
        return false;

    if(!pDecodeCtx->running) {
        pDecodeCtx->StartRunning(wCfg);
    }

    if(pDecodeCtx->eos ) {
        std::cout << "eos before GetFrameFromQ" << std::endl;
        return false;
    }

    CV_LOG_DEBUG(NULL, "VCU2 nextFrame called (placeholder implementation)");
    pFrame = pDecodeCtx->GetFrameFromQ();
    if(pFrame == nullptr ) {
        std::cout << "GetFrameFromQ is nullptr" << std::endl;
        return false;
    }

    if(pDecodeCtx->eos ) {
        std::cout << "eos after GetFrameFromQ" << std::endl;
        return false;
    }

    retrieveVideoFrame(frame, pFrame, frame_info);
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

} // namespace vcucodec
} // namespace cv
