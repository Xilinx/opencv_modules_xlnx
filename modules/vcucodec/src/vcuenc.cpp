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
#include "vcuenc.hpp"

#include "private/vcuutils.hpp"

namespace cv {
namespace vcucodec {

VCUEncoder::~VCUEncoder()
{
    auto pAllocator = pIpDevice->GetAllocator();
    AL_Allocator_Free(pAllocator, cfg.Settings.hRcPluginDmaContext);
    enc.reset();
    pLayerResources[0].reset();
    AL_Lib_Encoder_DeInit();
}

VCUEncoder::VCUEncoder(const String& filename, const EncoderInitParams& params) : filename_(filename), params_(params)
{
    SetDefaults(cfg);
    cfg.BitstreamFileName = filename;
    cfg.eSrcFormat = AL_SRC_FORMAT_RASTER;
    cfg.MainInput.YUVFileName = "../video/Crowd_Run_1280_720_Y800.yuv";
    cfg.MainInput.FileInfo.FourCC = FOURCC(NV12);
    if(cfg.MainInput.FileInfo.FourCC == FOURCC(NV12))
        cfg.Settings.tChParam[0].ePicFormat = AL_420_8BITS;
    else if(cfg.MainInput.FileInfo.FourCC == FOURCC(Y800))
        cfg.Settings.tChParam[0].ePicFormat = AL_400_8BITS;
    cfg.MainInput.FileInfo.FrameRate = 60;
    cfg.MainInput.FileInfo.PictHeight = 720;
    cfg.MainInput.FileInfo.PictWidth = 1280;
    SetCodingResolution(cfg);
    pLayerResources.emplace_back(std::make_unique<LayerResources>());
    enc = CtrlswEncOpen(cfg, pLayerResources, pIpDevice);
}

void VCUEncoder::write(InputArray frame)
{
    if(!frame.isMat()) {
        return;
    }
    
    cv::Size size = frame.size();
    IFrameSink* sink = enc.get();
    AL_TDimension tUpdatedDim = AL_TDimension { AL_GetSrcWidth(cfg.Settings.tChParam[0]), AL_GetSrcHeight(cfg.Settings.tChParam[0])};
    shared_ptr<AL_TBuffer> sourceBuffer = pLayerResources[0]->SrcBufPool.GetSharedBuffer();
    AL_PixMapBuffer_SetDimension(sourceBuffer.get(), tUpdatedDim);
    if(AL_PixMapBuffer_GetFourCC(sourceBuffer.get()) == FOURCC(NV12))
    {
        char* pY = reinterpret_cast<char*>(AL_PixMapBuffer_GetPlaneAddress(sourceBuffer.get(), AL_PLANE_Y));
        int32_t ySize = size.width * size.height * 2 / 3;
        memcpy(pY, (char*)frame.getMat().data, ySize);
        char* pUV = reinterpret_cast<char*>(AL_PixMapBuffer_GetPlaneAddress(sourceBuffer.get(), AL_PLANE_UV));
        memcpy(pUV, (char*)frame.getMat().data + ySize, ySize / 2);
    }
    sink->ProcessFrame(sourceBuffer.get());
}

bool VCUEncoder::set(int propId, double value)
{

}

double VCUEncoder::get(int propId) const
{
    double result = 0.0;
    return result; // Placeholder implementation
}


}  // namespace vcucodec
}  // namespace cv
