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
#include "ctrlsw_enc.hpp"

#include "private/vcuutils.hpp"
#include <array>
namespace cv {
namespace vcucodec {
namespace { // anonymous

std::map<String, AL_EProfile> hevcProfiles =
{
    {"MONO12", AL_PROFILE_HEVC_MONO12},
    {"MONO10", AL_PROFILE_HEVC_MONO10},
    {"MONO", AL_PROFILE_HEVC_MONO},
    {"MAIN_444_STILL", AL_PROFILE_HEVC_MAIN_444_STILL},
    {"MAIN_444_10_INTRA", AL_PROFILE_HEVC_MAIN_444_10_INTRA},
    {"MAIN_444_INTRA", AL_PROFILE_HEVC_MAIN_444_INTRA},
    {"MAIN_444_10", AL_PROFILE_HEVC_MAIN_444_10},
    {"MAIN_444", AL_PROFILE_HEVC_MAIN_444},
    {"MAIN_444_12", AL_PROFILE_HEVC_MAIN_444_12},
    {"MAIN_422_10_INTRA", AL_PROFILE_HEVC_MAIN_422_10_INTRA},
    {"MAIN_422_10", AL_PROFILE_HEVC_MAIN_422_10},
    {"MAIN_422_12", AL_PROFILE_HEVC_MAIN_422_12},
    {"MAIN_422", AL_PROFILE_HEVC_MAIN_422},
    {"MAIN_INTRA", AL_PROFILE_HEVC_MAIN_INTRA},
    {"MAIN_STILL", AL_PROFILE_HEVC_MAIN_STILL},
    {"MAIN10_INTRA", AL_PROFILE_HEVC_MAIN10_INTRA},
    {"MAIN10", AL_PROFILE_HEVC_MAIN10},
    {"MAIN12", AL_PROFILE_HEVC_MAIN12},
    {"MAIN", AL_PROFILE_HEVC_MAIN}
};

std::map<String, AL_EProfile> avcProfiles =
{
    {"BASELINE", AL_PROFILE_AVC_BASELINE},
    {"C_BASELINE", AL_PROFILE_AVC_C_BASELINE},
    {"MAIN", AL_PROFILE_AVC_MAIN},
    {"HIGH10_INTRA", AL_PROFILE_AVC_HIGH10_INTRA},
    {"HIGH10", AL_PROFILE_AVC_HIGH10},
    {"HIGH_422_INTRA", AL_PROFILE_AVC_HIGH_422_INTRA},
    {"HIGH_422", AL_PROFILE_AVC_HIGH_422},
    {"HIGH", AL_PROFILE_AVC_HIGH},
    {"C_HIGH", AL_PROFILE_AVC_C_HIGH},
    {"PROG_HIGH", AL_PROFILE_AVC_PROG_HIGH},
    {"CAVLC_444_INTRA", AL_PROFILE_AVC_CAVLC_444_INTRA},
    {"CAVLC_444", AL_PROFILE_AVC_CAVLC_444_INTRA},
    {"HIGH_444_INTRA", AL_PROFILE_AVC_HIGH_444_INTRA},
    {"HIGH_444_PRED", AL_PROFILE_AVC_HIGH_444_PRED},
    {"X_HIGH10_INTRA_CBG", AL_PROFILE_XAVC_HIGH10_INTRA_CBG},
    {"X_HIGH10_INTRA_VBR", AL_PROFILE_XAVC_HIGH10_INTRA_VBR},
    {"X_HIGH_422_INTRA_CBG", AL_PROFILE_XAVC_HIGH_422_INTRA_CBG},
    {"X_HIGH_422_INTRA_VBR", AL_PROFILE_XAVC_HIGH_422_INTRA_VBR},
    {"X_LONG_GOP_MAIN_MP4", AL_PROFILE_XAVC_LONG_GOP_MAIN_MP4},
    {"X_LONG_GOP_HIGH_MP4", AL_PROFILE_XAVC_LONG_GOP_HIGH_MP4},
    {"X_LONG_GOP_HIGH_MXF", AL_PROFILE_XAVC_LONG_GOP_HIGH_MXF},
    {"X_LONG_GOP_HIGH_422_MXF", AL_PROFILE_XAVC_LONG_GOP_HIGH_422_MXF}
};

std::map<String, uint8_t> levelsAvc =
{
    {"0.9",  9},
    {"1.0", 10}, {"1.1", 11}, {"1.2", 12}, {"1.3", 13},
    {"2.0", 21}, {"2.1", 22}, {"2.2", 23}, {"3.0", 30}, {"3.1", 31}, {"3.2", 32},
    {"4.0", 40}, {"4.1", 41}, {"4.2", 42},
    {"5.0", 50}, {"5.1", 51}, {"5.2", 52},
    {"6.0", 60}, {"6.1", 61}, {"6.2", 62},
};

std::map<String, uint8_t> levelsHevc =
{
    {"1.0", 10},
    {"2.0", 20}, {"2.1", 21},
    {"3.0", 30}, {"3.1", 31},
    {"4.0", 40}, {"4.1", 41},
    {"5.0", 50}, {"5.1", 51}, {"5.2", 52},
    {"6.0", 60}, {"6.1", 61}, {"6.2", 62}
};


AL_EProfile getProfile(Codec codec, String profile)
{
    AL_EProfile profileEnum = AL_PROFILE_UNKNOWN;
    switch (codec)
    {
    case Codec::HEVC:
        if (auto it = hevcProfiles.find(profile); it != hevcProfiles.end())
            profileEnum = it->second;
        break;
    case Codec::AVC:
        if (auto it = avcProfiles.find(profile); it != avcProfiles.end())
            profileEnum = it->second;
        break;
#ifdef HAVE_VCU2_CTRLSW
    case Codec::JPEG:
        profileEnum = AL_PROFILE_JPEG_EXT_HUFF;
#endif
    }
    return profileEnum;
}

uint8_t getLevel(Codec codec, String level)
{
    uint8_t codecVal = 0;
    switch (codec)
    {
    case Codec::HEVC:
        if (auto it = levelsHevc.find(level); it != levelsHevc.end())
            codecVal = it->second;
        break;
    case Codec::AVC:
        if (auto it = levelsAvc.find(level); it != levelsAvc.end())
            codecVal = it->second;
        break;
    case Codec::JPEG:
        break;
    }
    return codecVal;
}



} // anonymous namespace

VCUEncoder::~VCUEncoder()
{
    auto pAllocator = pIpDevice->GetAllocator();
    AL_Allocator_Free(pAllocator, cfg.Settings.hRcPluginDmaContext);
    enc.reset();
    pLayerResources[0].reset();
    AL_Lib_Encoder_DeInit();
}

VCUEncoder::VCUEncoder(const String& filename, const EncoderInitParams& params)
: filename_(filename), params_(params)
{
    AL_EProfile profile = getProfile(params.codec, params.profileSettings.profile);
    (void) profile; // TODO
    uint8_t level = getLevel(params.codec, params.profileSettings.level);
    (void) level; // TODO
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
    cfg.MainInput.FileInfo.PictHeight = params.pictHeight;
    cfg.MainInput.FileInfo.PictWidth = params.pictWidth;
    cfg.Settings.tChParam[0].tRCParam.eRCMode = (AL_ERateCtrlMode)params.rcMode;
    cfg.Settings.tChParam[0].tRCParam.uTargetBitRate = params.bitrate * 1000;
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
    AL_TDimension tUpdatedDim = AL_TDimension { AL_GetSrcWidth(cfg.Settings.tChParam[0]),
                                                AL_GetSrcHeight(cfg.Settings.tChParam[0])};
    shared_ptr<AL_TBuffer> sourceBuffer = pLayerResources[0]->SrcBufPool.GetSharedBuffer();
    AL_PixMapBuffer_SetDimension(sourceBuffer.get(), tUpdatedDim);
    if(AL_PixMapBuffer_GetFourCC(sourceBuffer.get()) == FOURCC(NV12))
    {
        char* pY = reinterpret_cast<char*>(AL_PixMapBuffer_GetPlaneAddress(sourceBuffer.get(),
                                                                           AL_PLANE_Y));
        int32_t ySize = size.width * size.height * 2 / 3;
        memcpy(pY, (char*)frame.getMat().data, ySize);
        char* pUV = reinterpret_cast<char*>(AL_PixMapBuffer_GetPlaneAddress(sourceBuffer.get(),
                                                                            AL_PLANE_UV));
        memcpy(pUV, (char*)frame.getMat().data + ySize, ySize / 2);
    }
    sink->ProcessFrame(sourceBuffer.get());
}

bool VCUEncoder::set(int propId, double value)
{
    (void)propId;
    (void)value;
    return false;
}

double VCUEncoder::get(int propId) const
{
    (void)propId;
    double result = 0.0;
    return result; // Placeholder implementation
}

// Static functions

String Encoder::getProfiles(Codec codec)
{
    if (codec == Codec::JPEG) {
        return "JPEG"; // JPEG does not have profiles
    }
    String profiles;
    std::map<std::string, AL_EProfile>& profilesMap =
        (codec == Codec::HEVC) ? hevcProfiles : avcProfiles;
    bool first = true;
    for (const auto& profile : profilesMap) {
        if (first) {
            profiles += profile.first;
            first = false;
        }
        else
        {
            profiles += ',';
            profiles += profile.first;
        }
    }
    return profiles;
}

String Encoder::getLevels(Codec codec)
{
    if (codec == Codec::JPEG) {
        return ""; // JPEG does not have levels
    }
    std::map<String, uint8_t>& levelsMap = (codec == Codec::HEVC) ? levelsHevc : levelsAvc;
    String levels;
    bool first = true;
    for (const auto& level : levelsMap) {
        if (first)
        {
            levels += level.first;
            first = false;
        }
        else
        {
            levels += ',';
            levels += level.first;
        }
    }
    return levels;
}

}  // namespace vcucodec
}  // namespace cv
