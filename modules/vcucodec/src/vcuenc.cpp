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

#include "vcuutils.hpp"
#include "vcucommand.hpp"
#include "vcudevice.hpp"
#include "vcuenccontext.hpp"
#include "vcuframe.hpp"

extern "C" {
#include "lib_common/PixMapBuffer.h"
}

#include <array>
#include <map>
#include <iostream>

#define CHECK(statement) \
  if(!statement) \
    std::cerr << # statement << " failed with error : " \
    << static_cast<std::underlying_type_t<AL_ERR>>(AL_Encoder_GetLastError(hEnc_)) << std::endl

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

void setCodingResolution(EncContext::Config& cfg)
{
    int32_t iMaxSrcWidth = cfg.MainInput.FileInfo.PictWidth;
    int32_t iMaxSrcHeight = cfg.MainInput.FileInfo.PictHeight;

    for(auto const& input: cfg.DynamicInputs)
    {
        iMaxSrcWidth = max(input.FileInfo.PictWidth, iMaxSrcWidth);
        iMaxSrcHeight = max(input.FileInfo.PictHeight, iMaxSrcHeight);
    }

    cfg.Settings.tChParam[0].uSrcWidth = iMaxSrcWidth;
    cfg.Settings.tChParam[0].uSrcHeight = iMaxSrcHeight;

    cfg.Settings.tChParam[0].uEncWidth = cfg.Settings.tChParam[0].uSrcWidth;
    cfg.Settings.tChParam[0].uEncHeight = cfg.Settings.tChParam[0].uSrcHeight;

    if(cfg.Settings.tChParam[0].bEnableSrcCrop)
    {
        cfg.Settings.tChParam[0].uEncWidth = cfg.Settings.tChParam[0].uSrcCropWidth;
        cfg.Settings.tChParam[0].uEncHeight = cfg.Settings.tChParam[0].uSrcCropHeight;
    }
}

class DefaultEncoderCallback : public EncoderCallback
{
public:
    DefaultEncoderCallback(const String& filename) : output_(filename, true)
    {
    }

    virtual ~DefaultEncoderCallback() override {}

    virtual void onEncoded(std::vector<std::string_view>& encodedData) override
    {
        for (const auto& str : encodedData)
        {
            output_().write(str.data(), str.size());
        }
    }

    virtual void onFinished() override
    {
        output_().close();
    }

private:
    OutputStream output_;

};

} // anonymous namespace

VCUEncoder::~VCUEncoder()
{
    auto pAllocator = device_->getAllocator();
    AL_Allocator_Free(pAllocator, cfg_->Settings.hRcPluginDmaContext);
    enc_.reset();
}

VCUEncoder::VCUEncoder(const String& filename, const EncoderInitParams& params,
                       Ptr<EncoderCallback> callback)
: filename_(filename), params_(params), callback_(callback), currentFrameIndex_(0), hEnc_(nullptr)
{
    initSettings(params);
    if (!validateSettings())
        return;

    AL_EProfile profile = getProfile(currentSettings_.pic_.codec, currentSettings_.profile_.profile);
    uint8_t level = getLevel(currentSettings_.pic_.codec, currentSettings_.profile_.level);
    cfg_.reset(new EncContext::Config);
    EncContext::Config& cfg = *cfg_;

    // Initialize defaults
    cfg.RecFourCC = FOURCC(NULL);
    AL_Settings_SetDefaults(&cfg.Settings);

    // Set codec-specific defaults (QP bounds, codec parameters)
    AL_Settings_SetDefaultParam(&cfg.Settings);

    cfg.RunInfo.encDevicePaths = ENCODER_DEVICES;
#ifdef HAVE_VCU2_CTRLSW
    cfg.RunInfo.eDeviceType = AL_EDeviceType::AL_DEVICE_TYPE_EMBEDDED;
    cfg.RunInfo.eSchedulerType = AL_ESchedulerType::AL_SCHEDULER_TYPE_CPU;
#elif defined(HAVE_VCU_CTRLSW)
    cfg.RunInfo.eDeviceType = AL_EDeviceType::AL_DEVICE_TYPE_BOARD;
    cfg.RunInfo.eSchedulerType = AL_ESchedulerType::AL_SCHEDULER_TYPE_MCU;
#endif
    cfg.RunInfo.bLoop = false;
    cfg.RunInfo.iMaxPict = INT32_MAX; // ALL
    cfg.RunInfo.iFirstPict = 0;
    cfg.RunInfo.iScnChgLookAhead = 3;
    cfg.RunInfo.ipCtrlMode = AL_EIpCtrlMode::AL_IPCTRL_MODE_STANDARD;
    cfg.RunInfo.uInputSleepInMilliseconds = 0;
    cfg.iForceStreamBufSize = 0;

    cfg.eSrcFormat = AL_SRC_FORMAT_RASTER;
    cfg.MainInput.YUVFileName = "../video/Crowd_Run_1280_720_Y800.yuv";
    cfg.MainInput.FileInfo.FourCC = currentSettings_.pic_.fourcc;
    cfg.MainInput.FileInfo.FrameRate = currentSettings_.pic_.framerate;
    cfg.MainInput.FileInfo.PictHeight = currentSettings_.pic_.height;
    cfg.MainInput.FileInfo.PictWidth = currentSettings_.pic_.width;

    // Set picture format based on FourCC
    if(cfg.MainInput.FileInfo.FourCC == FOURCC(NV12))
        cfg.Settings.tChParam[0].ePicFormat = AL_420_8BITS;
    else if(cfg.MainInput.FileInfo.FourCC == FOURCC(P010))
    {
        cfg.Settings.tChParam[0].ePicFormat = AL_420_10BITS;
        AL_SET_BITDEPTH(&cfg.Settings.tChParam[0].ePicFormat, 10);
        cfg.Settings.tChParam[0].uSrcBitDepth = AL_GET_BITDEPTH(cfg.Settings.tChParam[0].ePicFormat);
        cfg.Settings.tChParam[0].eProfile = AL_PROFILE_HEVC_MAIN10;
    }
    else if(cfg.MainInput.FileInfo.FourCC == FOURCC(P012))
    {
        cfg.Settings.tChParam[0].ePicFormat = AL_420_12BITS;
        AL_SET_BITDEPTH(&cfg.Settings.tChParam[0].ePicFormat, 12);
        cfg.Settings.tChParam[0].uSrcBitDepth = AL_GET_BITDEPTH(cfg.Settings.tChParam[0].ePicFormat);
        cfg.Settings.tChParam[0].eProfile = AL_PROFILE_HEVC_MAIN12;
    }
    else if(cfg.MainInput.FileInfo.FourCC == FOURCC(NV16))
        cfg.Settings.tChParam[0].ePicFormat = AL_422_8BITS;
    else if(cfg.MainInput.FileInfo.FourCC == FOURCC(P210))
    {
        cfg.Settings.tChParam[0].ePicFormat = AL_422_10BITS;
        AL_SET_BITDEPTH(&cfg.Settings.tChParam[0].ePicFormat, 10);
        cfg.Settings.tChParam[0].uSrcBitDepth = AL_GET_BITDEPTH(cfg.Settings.tChParam[0].ePicFormat);
        cfg.Settings.tChParam[0].eProfile = AL_PROFILE_HEVC_MAIN_422_10;
    }
    else if(cfg.MainInput.FileInfo.FourCC == FOURCC(P212))
    {
        cfg.Settings.tChParam[0].ePicFormat = AL_422_12BITS;
        AL_SET_BITDEPTH(&cfg.Settings.tChParam[0].ePicFormat, 12);
        cfg.Settings.tChParam[0].uSrcBitDepth = AL_GET_BITDEPTH(cfg.Settings.tChParam[0].ePicFormat);
        cfg.Settings.tChParam[0].eProfile = AL_PROFILE_HEVC_MAIN_422_12;
    }
    else if(cfg.MainInput.FileInfo.FourCC == FOURCC(Y800))
        cfg.Settings.tChParam[0].ePicFormat = AL_400_8BITS;
    else if(cfg.MainInput.FileInfo.FourCC == FOURCC(I444))
    {
        cfg.Settings.tChParam[0].ePicFormat = AL_444_8BITS;
        cfg.Settings.tChParam[0].eProfile = AL_PROFILE_HEVC_MAIN_444;
    }

    // Apply profile and level if specified
    if (profile != AL_PROFILE_UNKNOWN)
        cfg.Settings.tChParam[0].eProfile = profile;
    if (level != 0)
        cfg.Settings.tChParam[0].uLevel = level;

    // Rate Control settings from currentSettings_.rc_
    cfg.Settings.tChParam[0].tRCParam.eRCMode = (AL_ERateCtrlMode)currentSettings_.rc_.mode;
    cfg.Settings.tChParam[0].tRCParam.uTargetBitRate = currentSettings_.rc_.bitrate * 1000; // Convert kbps to bps
    cfg.Settings.tChParam[0].tRCParam.uMaxBitRate = currentSettings_.rc_.maxBitrate * 1000; // Convert kbps to bps
    cfg.Settings.tChParam[0].tRCParam.uCPBSize = currentSettings_.rc_.cpbSize;
    cfg.Settings.tChParam[0].tRCParam.uInitialRemDelay = currentSettings_.rc_.initialDelay;
    // Note: iMaxQuality is not part of AL_TRCParam, maxQualityTarget not directly mappable

    // GOP settings from currentSettings_.gop_
    cfg.Settings.tChParam[0].tGopParam.uGopLength = currentSettings_.gop_.gopLength;
    cfg.Settings.tChParam[0].tGopParam.uNumB = currentSettings_.gop_.nrBFrames;
    cfg.Settings.tChParam[0].tGopParam.eMode = (AL_EGopCtrlMode)currentSettings_.gop_.mode;
    cfg.Settings.tChParam[0].tGopParam.eGdrMode = (AL_EGdrMode)currentSettings_.gop_.gdrMode;
    cfg.Settings.tChParam[0].tGopParam.bEnableLT = currentSettings_.gop_.longTermRef;
    cfg.Settings.tChParam[0].tGopParam.uFreqLT = currentSettings_.gop_.longTermFreq;
    cfg.Settings.tChParam[0].tGopParam.uFreqIDR = currentSettings_.gop_.periodIDR;

    // Override filler data setting from RCSettings
    cfg.Settings.eEnableFillerData = currentSettings_.rc_.fillerData ? AL_FILLER_ENC : AL_FILLER_DISABLE;

    // Override AUD setting - disable by default (can be made configurable later)
    cfg.Settings.bEnableAUD = false;

    // Frame rate fallback
    if (cfg.MainInput.FileInfo.FrameRate == 0)
        cfg.MainInput.FileInfo.FrameRate = cfg.Settings.tChParam[0].tRCParam.uFrameRate;

    // Calculate reconstruction FourCC if not set
    if (cfg.RecFourCC == FOURCC(NULL))
    {
        cfg.RecFourCC = cfg.MainInput.FileInfo.FourCC;

        // Adjust based on picture format
        auto picFormat = cfg.Settings.tChParam[0].ePicFormat;
        auto chromaMode = AL_GET_CHROMA_MODE(picFormat);
        auto bitDepth = AL_GET_BITDEPTH(picFormat);

        if (chromaMode == AL_CHROMA_4_2_0 && bitDepth == 8)
            cfg.RecFourCC = FOURCC(NV12);
        else if (chromaMode == AL_CHROMA_4_2_0 && bitDepth == 10)
            cfg.RecFourCC = FOURCC(P010);
        else if (chromaMode == AL_CHROMA_4_2_0 && bitDepth == 12)
            cfg.RecFourCC = FOURCC(P012);
        else if (chromaMode == AL_CHROMA_4_2_2 && bitDepth == 8)
            cfg.RecFourCC = FOURCC(NV16);
        else if (chromaMode == AL_CHROMA_4_2_2 && bitDepth == 10)
            cfg.RecFourCC = FOURCC(P210);
        else if (chromaMode == AL_CHROMA_4_2_2 && bitDepth == 12)
            cfg.RecFourCC = FOURCC(P212);
        else if (chromaMode == AL_CHROMA_4_0_0 && bitDepth == 8)
            cfg.RecFourCC = FOURCC(Y800);
        else if (chromaMode == AL_CHROMA_4_4_4 && bitDepth == 8)
            cfg.RecFourCC = FOURCC(I444);
    }

    // Validate reconstruction format storage mode
    auto recStorageMode = AL_GetStorageMode(cfg.RecFourCC);
    if (recStorageMode != AL_FB_RASTER)
        throw std::runtime_error("Reconstruction format must be raster");

    // Convert source format to source mode and set for all layers
    AL_ESrcMode eSrcMode;
    switch(cfg.eSrcFormat)
    {
    case AL_SRC_FORMAT_RASTER:
        eSrcMode = AL_SRC_RASTER;
        break;
#ifdef HAVE_VCU2_CTRLSW
    case AL_SRC_FORMAT_RASTER_MSB:
        eSrcMode = AL_SRC_RASTER_MSB;
        break;
    case AL_SRC_FORMAT_TILE_64x4:
        eSrcMode = AL_SRC_TILE_64x4;
        break;
    case AL_SRC_FORMAT_TILE_32x4:
        eSrcMode = AL_SRC_TILE_32x4;
        break;
#endif
    default:
        throw std::runtime_error("Unsupported source format.");
    }

    for (uint8_t uLayer = 0; uLayer < cfg.Settings.NumLayer; uLayer++)
        cfg.Settings.tChParam[uLayer].eSrcMode = eSrcMode;

    // Enable reconstruction output if RecFileName is set (will be set later when creating context)
    // Note: RecFileName is empty at this point, so AL_OPT_FORCE_REC will be set in EncoderContext if needed

    setCodingResolution(cfg);
    if (!callback_)
    {
        callback_.reset(new DefaultEncoderCallback(filename_));
    }

    enc_ = EncContext::create(cfg_, device_,
        [this](std::vector<std::string_view>& data)
        {
            callback_->onEncoded(data);
        });
    if (enc_)
    {
        hEnc_ = enc_->hEnc();

        // Apply initial global motion vector if provided
        if (currentSettings_.gmv_.frameIndex >= 0)
        {
            enc_->notifyGMV(currentSettings_.gmv_.frameIndex,
                           currentSettings_.gmv_.gmVectorX,
                           currentSettings_.gmv_.gmVectorY);
        }
    }
    settingsString_ = currentSettingsString();
    //printf("VCUEncoder created with settings:\n%s\n", currentSettingsString().c_str());
}

void VCUEncoder::write(InputArray frame)
{
    if(!frame.isMat()) {
        return;
    }

    // Execute any pending commands for this frame
    commandQueue_.execute(currentFrameIndex_);

    cv::Mat mat = frame.getMat();
    AL_TDimension tUpdatedDim = AL_TDimension { AL_GetSrcWidth(cfg_->Settings.tChParam[0]),
                                                AL_GetSrcHeight(cfg_->Settings.tChParam[0]) };
    auto sourceBuffer = enc_->getSharedBuffer();
    Ptr<Frame> vcuFrame = Frame::createFromMat(sourceBuffer, mat, tUpdatedDim);

    enc_->writeFrame(vcuFrame);

    // Increment frame index for next frame
    currentFrameIndex_++;
}

bool VCUEncoder::eos()
{
    // Trigger end of stream by sending nullptr (flush signal)
    enc_->writeFrame(nullptr);

    // Wait for encoding to complete (max 1 second)
    return enc_->waitForCompletion();
}

String VCUEncoder::settings() const
{
    return settingsString_;
}

String VCUEncoder::statistics() const
{
    return enc_? enc_->statistics() : String();
}

bool VCUEncoder::set(int propId, double value)
{
    std::lock_guard lock(settingsMutex_);
    (void)propId;
    (void)value;
    return false;
}

double VCUEncoder::get(int propId) const
{
    std::lock_guard lock(settingsMutex_);
    (void)propId;
    double result = 0.0;
    return result; // Placeholder implementation
}

void VCUEncoder::set(const RCSettings& rcSettings)
{
    std::lock_guard lock(settingsMutex_);
    currentSettings_.rc_ = rcSettings;
}

void VCUEncoder::get(RCSettings& rcSettings) const
{
    std::lock_guard lock(settingsMutex_);
    rcSettings = currentSettings_.rc_;
}

void VCUEncoder::set(const GOPSettings& gopSettings)
{
    std::lock_guard lock(settingsMutex_);
    currentSettings_.gop_ = gopSettings;
}

void VCUEncoder::get(GOPSettings& gopSettings) const
{
    std::lock_guard lock(settingsMutex_);
    gopSettings = currentSettings_.gop_;
}

void VCUEncoder::set(const GlobalMotionVector& gmVector)
{
    std::lock_guard lock(settingsMutex_);
    currentSettings_.gmv_ = gmVector;
    enc_->notifyGMV(gmVector.frameIndex, gmVector.gmVectorX, gmVector.gmVectorY);
}

void VCUEncoder::get(GlobalMotionVector& gmVector) const
{
    std::lock_guard lock(settingsMutex_);
    gmVector = currentSettings_.gmv_;
}

void VCUEncoder::set(const ProfileSettings& profileSettings)
{
    std::lock_guard lock(settingsMutex_);
    currentSettings_.profile_ = profileSettings;
}

void VCUEncoder::get(ProfileSettings& profileSettings) const
{
    std::lock_guard lock(settingsMutex_);
    profileSettings = currentSettings_.profile_;
}

//
// Dynamic commands
//

void VCUEncoder::setSceneChange(int32_t frameIdx, int32_t lookAhead)
{
    Command cmd = { frameIdx, false,
        [this, lookAhead](){ AL_Encoder_NotifySceneChange(hEnc_, lookAhead); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setIsLongTerm(int32_t frameIdx)
{
    Command cmd = { frameIdx, false, [this](){ AL_Encoder_NotifyIsLongTerm(hEnc_); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setUseLongTerm(int32_t frameIdx)
{
    Command cmd = { frameIdx, false, [this](){ AL_Encoder_NotifyUseLongTerm(hEnc_); }};
    commandQueue_.push(cmd);
}

#ifdef HAVE_VCU2_CTRLSW
void VCUEncoder::setIsSkip(int32_t frameIdx)
{
    Command cmd = { frameIdx, false, [this](){ AL_Encoder_NotifyIsSkip(hEnc_); }};
    commandQueue_.push(cmd);
}
#else
void VCUEncoder::setIsSkip(int32_t frameIdx)
{
    (void) frameIdx;
    std::cerr << "Warning: Skip is only supported on VCU2 devices." << std::endl;
}
#endif

#ifdef HAVE_VCU2_CTRLSW
void VCUEncoder::setSAO(int32_t frameIdx, bool bSAOEnabled)
{
    Command cmd = { frameIdx, false,
                    [this, bSAOEnabled](){ CHECK(AL_Encoder_SetSAO(hEnc_, bSAOEnabled)); }};
    commandQueue_.push(cmd);
}
#else
void VCUEncoder::setSAO(int32_t frameIdx, bool bSAOEnabled)
{
    (void) frameIdx;
    (void) bSAOEnabled;
    if (bSAOEnabled)
        std::cerr << "Warning: SAO is only supported on VCU2 devices." << std::endl;
}
#endif

void VCUEncoder::restartGop(int32_t frameIdx)
{
    Command cmd = { frameIdx, false, [this](){ CHECK(AL_Encoder_RestartGop(hEnc_)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::restartGopRecoveryPoint(int32_t frameIdx)
{
    Command cmd = { frameIdx, false, [this](){ CHECK(AL_Encoder_RestartGopRecoveryPoint(hEnc_)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setGopLength(int32_t frameIdx, int32_t gopLength)
{
    Command cmd = { frameIdx, false,
                    [this, gopLength](){ CHECK(AL_Encoder_SetGopLength(hEnc_, gopLength)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setNumB(int32_t frameIdx, int32_t numB)
{
    Command cmd = { frameIdx, false, [this, numB](){ CHECK(AL_Encoder_SetGopNumB(hEnc_, numB)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setFreqIDR(int32_t frameIdx, int32_t freqIDR)
{
    Command cmd = { frameIdx, false,
                    [this, freqIDR](){ CHECK(AL_Encoder_SetFreqIDR(hEnc_, freqIDR)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setFrameRate(int32_t frameIdx, int32_t frameRate, int32_t clockRatio)
{
    Command cmd = { frameIdx, false,
        [this, frameRate, clockRatio]()
        {
            CHECK(AL_Encoder_SetFrameRate(hEnc_, frameRate, clockRatio));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setBitRate(int32_t frameIdx, int32_t bitRate)
{
    Command cmd = { frameIdx, false,
                    [this, bitRate](){ CHECK(AL_Encoder_SetBitRate(hEnc_, bitRate)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setMaxBitRate(int32_t frameIdx, int32_t iTargetBitRate, int32_t iMaxBitRate)
{
    Command cmd = { frameIdx, false,
        [this, iTargetBitRate, iMaxBitRate]()
        {
            CHECK(AL_Encoder_SetMaxBitRate(hEnc_, iTargetBitRate, iMaxBitRate));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQP(int32_t frameIdx, int32_t qp)
{
    Command cmd = { frameIdx, false, [this, qp](){ CHECK(AL_Encoder_SetQP(hEnc_, qp)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQPOffset(int32_t frameIdx, int32_t iQpOffset)
{
    Command cmd = { frameIdx, false,
        [this, iQpOffset]()
        {
            CHECK(AL_Encoder_SetQPOffset(hEnc_, iQpOffset));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQPBounds(int32_t frameIdx, int32_t iMinQP, int32_t iMaxQP)
{
    Command cmd = { frameIdx, false,
        [this, iMinQP, iMaxQP](){ CHECK(AL_Encoder_SetQPBounds(hEnc_, iMinQP, iMaxQP)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQPBoundsI(int32_t frameIdx, int32_t iMinQP_I, int32_t iMaxQP_I)
{
    Command cmd = { frameIdx, false,
        [this, iMinQP_I, iMaxQP_I]()
        {
            CHECK(AL_Encoder_SetQPBoundsPerFrameType(hEnc_, iMinQP_I, iMaxQP_I, AL_SLICE_I));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQPBoundsP(int32_t frameIdx, int32_t iMinQP_P, int32_t iMaxQP_P)
{
    Command cmd = { frameIdx, false,
        [this, iMinQP_P, iMaxQP_P]()
        {
            CHECK(AL_Encoder_SetQPBoundsPerFrameType(hEnc_, iMinQP_P, iMaxQP_P, AL_SLICE_P));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQPBoundsB(int32_t frameIdx, int32_t iMinQP_B, int32_t iMaxQP_B)
{
    Command cmd = { frameIdx, false,
        [this, iMinQP_B, iMaxQP_B]()
        {
            CHECK(AL_Encoder_SetQPBoundsPerFrameType(hEnc_, iMinQP_B, iMaxQP_B, AL_SLICE_B));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQPIPDelta(int32_t frameIdx, int32_t iQPDelta)
{
    Command cmd = { frameIdx, false,
                    [this, iQPDelta](){ CHECK(AL_Encoder_SetQPIPDelta(hEnc_, iQPDelta)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQPPBDelta(int32_t frameIdx, int32_t iQPDelta)
{
    Command cmd = { frameIdx, false,
                    [this, iQPDelta](){ CHECK(AL_Encoder_SetQPPBDelta(hEnc_, iQPDelta)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setLFMode(int32_t frameIdx, int32_t iMode)
{
    Command cmd = { frameIdx, false,
                    [this, iMode](){ CHECK(AL_Encoder_SetLoopFilterMode(hEnc_, iMode)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setLFBetaOffset(int32_t frameIdx, int32_t iBetaOffset)
{
    Command cmd = { frameIdx, false,
        [this, iBetaOffset](){ CHECK(AL_Encoder_SetLoopFilterBetaOffset(hEnc_, iBetaOffset)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setLFTcOffset(int32_t frameIdx, int32_t iTcOffset)
{
    Command cmd = { frameIdx, false,
        [this, iTcOffset](){ CHECK(AL_Encoder_SetLoopFilterTcOffset(hEnc_, iTcOffset)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setCostMode(int32_t frameIdx, bool bCostMode)
{
    Command cmd = { frameIdx, false,
                    [this, bCostMode](){ CHECK(AL_Encoder_SetCostMode(hEnc_, bCostMode)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setMaxPictureSize(int32_t frameIdx, int32_t iMaxPictureSize)
{
    Command cmd = { frameIdx, false,
        [this, iMaxPictureSize](){ CHECK(AL_Encoder_SetMaxPictureSize(hEnc_, iMaxPictureSize)); }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setMaxPictureSizeI(int32_t frameIdx, int32_t iMaxPictureSize_I)
{
    Command cmd = { frameIdx, false,
        [this, iMaxPictureSize_I]()
        {
            CHECK(AL_Encoder_SetMaxPictureSizePerFrameType(hEnc_, iMaxPictureSize_I, AL_SLICE_I));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setMaxPictureSizeP(int32_t frameIdx, int32_t iMaxPictureSize_P)
{
    Command cmd = { frameIdx, false,
        [this, iMaxPictureSize_P]()
        {
            CHECK(AL_Encoder_SetMaxPictureSizePerFrameType(hEnc_, iMaxPictureSize_P, AL_SLICE_P));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setMaxPictureSizeB(int32_t frameIdx, int32_t iMaxPictureSize_B)
{
    Command cmd = { frameIdx, false,
        [this, iMaxPictureSize_B]()
        {
            CHECK(AL_Encoder_SetMaxPictureSizePerFrameType(hEnc_, iMaxPictureSize_B, AL_SLICE_B));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setQPChromaOffsets(int32_t frameIdx, int32_t iQp1Offset, int32_t iQp2Offset)
{
    Command cmd = { frameIdx, false,
        [this, iQp1Offset, iQp2Offset]()
        {
            CHECK(AL_Encoder_SetQPChromaOffsets(hEnc_, iQp1Offset, iQp2Offset));
        }};
    commandQueue_.push(cmd);
}

void VCUEncoder::setAutoQP(int32_t frameIdx, bool bUseAutoQP)
{
    Command cmd = { frameIdx, false,
                    [this, bUseAutoQP](){ CHECK(AL_Encoder_SetAutoQP(hEnc_, bUseAutoQP)); }};
    commandQueue_.push(cmd);
}

#ifdef HAVE_VCU2_CTRLSW
void VCUEncoder::setAutoQPThresholdQPAndDeltaQP(int32_t frameIdx, bool bEnableUserAutoQPValues,
        std::vector<int> thresholdQP, std::vector<int> deltaQP)
{
    Command cmd = { frameIdx, false, [this, bEnableUserAutoQPValues, thresholdQP, deltaQP](){
        AL_TAutoQPCtrl tAutoQPCtrl;
        if (bEnableUserAutoQPValues) {
            for (int32_t i = 0; i < AL_QP_CTRL_MAX_NUM_THRESHOLDS
                                && i < static_cast<int32_t>(thresholdQP.size()); i++) {
                tAutoQPCtrl.thresholdQP[i] = thresholdQP[i];
            }
            for (int32_t i = 0; i < AL_QP_CTRL_MAX_NUM_THRESHOLDS
                                && i < static_cast<int32_t>(deltaQP.size()); i++) {
                tAutoQPCtrl.deltaQP[i] = deltaQP[i];
            }
            if (!deltaQP.empty()) {
                tAutoQPCtrl.deltaQP[AL_QP_CTRL_MAX_NUM_THRESHOLDS] = deltaQP.back();
            }
        }
        CHECK(AL_Encoder_SetAutoQPThresholdAndDelta(hEnc_, bEnableUserAutoQPValues, &tAutoQPCtrl));
    }};
    commandQueue_.push(cmd);
}
#else
void VCUEncoder::setAutoQPThresholdQPAndDeltaQP(int32_t frameIdx, bool bEnableUserAutoQPValues,
        std::vector<int> thresholdQP, std::vector<int> deltaQP)
{
    (void)frameIdx;
    (void)bEnableUserAutoQPValues;
    (void)thresholdQP;
    (void)deltaQP;

    std::cerr << "Warning: Auto QP thresholds and delta QP are only supported on VCU2 devices."
              << std::endl;
}
#endif

void VCUEncoder::setHDRIndex(int32_t frameIdx, int32_t iHDRIdx)
{
    Command cmd = { frameIdx, false, [this, iHDRIdx](){
        // HDR index change is similar to CommandsSender implementation
        // This sets a flag for HDR change that gets processed later
        // For now, we implement the direct version
        // Note: The actual HDR processing may require additional context
        // from the encoder's HDR management system
        (void)iHDRIdx; // Store for later processing if needed
    }};
    commandQueue_.push(cmd);
}

bool VCUEncoder::validateSettings()
{
    bool valid;
    const PictureEncSettings& pic = currentSettings_.pic_;
    const RCSettings& rc = currentSettings_.rc_;
    const GOPSettings& gop = currentSettings_.gop_;

    valid = pic.codec == Codec::HEVC || pic.codec == Codec::AVC;
    if (!valid) CV_Error(cv::Error::StsBadArg, "Unsupported codec");

    auto fi = FormatInfo(pic.fourcc);
    valid = fi.encodeable;
    if (!valid) CV_Error(cv::Error::StsBadArg, "Unsupported input fourcc");
    valid = rc.mode >= RCMode::CONST_QP && rc.mode <= RCMode::VBR;
    if (!valid) CV_Error(Error::StsBadArg, "Unsupported rate control mode");
    valid = rc.bitrate > 0;
    if (!valid) CV_Error(Error::StsBadArg, "Bitrate must be greater than 0");
    valid = gop.gopLength > 0;
    if (!valid) CV_Error(Error::StsBadArg, "GOP length must be greater than 0");
    valid = gop.nrBFrames >= 0;
    if (!valid) CV_Error(Error::StsBadArg, "Number of B-frames must be non-negative");
    valid = pic.width > 0 && pic.width <= 8192; // Max width 8K
    if (!valid) CV_Error(Error::StsBadArg, "Width must be in the range [1, 8192]");
    valid = pic.height > 0 && pic.height <= 2160; // Max height 4K
    if (!valid) CV_Error(Error::StsBadArg, "Height must be in the range [1, 2160]");

    return valid;
}

void VCUEncoder::initSettings(const EncoderInitParams& params)
{
    if (params.pictureEncSettings)
        currentSettings_.pic_ = *params.pictureEncSettings;
    if (params.rcSettings)
        currentSettings_.rc_ = *params.rcSettings;
    if (params.gopSettings)
        currentSettings_.gop_ = *params.gopSettings;
    if (params.profileSettings)
        currentSettings_.profile_ = *params.profileSettings;
    if (params.globalMotionVector)
        currentSettings_.gmv_ = *params.globalMotionVector;
}

String VCUEncoder::currentSettingsString() const
{
    std::lock_guard lock(settingsMutex_);

    std::string result;
    result += "Picture: codec=" + toString(currentSettings_.pic_.codec);
    result += ", fourcc=" + toString(currentSettings_.pic_.fourcc);
    result += ", width=" + toString(currentSettings_.pic_.width);
    result += ", height=" + toString(currentSettings_.pic_.height);
    result += ", framerate=" + toString(currentSettings_.pic_.framerate);

    result += "\nRate Control: mode=" + toString(currentSettings_.rc_.mode);
    result += ", entropy=" + toString(currentSettings_.rc_.entropy);
    result += ", bitrate=" + toString(currentSettings_.rc_.bitrate);
    result += ", maxBitrate=" + toString(currentSettings_.rc_.maxBitrate);
    result += ", cpbSize=" + toString(currentSettings_.rc_.cpbSize);
    result += ", initialDelay=" + toString(currentSettings_.rc_.initialDelay);
    result += ", fillerData=" + toString(currentSettings_.rc_.fillerData);
    result += ", maxQualityTarget=" + toString(currentSettings_.rc_.maxQualityTarget);
    result += ", maxPictureSizeI=" + toString(currentSettings_.rc_.maxPictureSizeI);
    result += ", maxPictureSizeP=" + toString(currentSettings_.rc_.maxPictureSizeP);
    result += ", maxPictureSizeB=" + toString(currentSettings_.rc_.maxPictureSizeB);
    result += ", skipFrame=" + toString(currentSettings_.rc_.skipFrame);
    result += ", maxSkip=" + toString(currentSettings_.rc_.maxSkip);

    result += "\nGOP: mode=" + toString(currentSettings_.gop_.mode);
    result += ", gdrMode=" + toString(currentSettings_.gop_.gdrMode);
    result += ", gopLength=" + toString(currentSettings_.gop_.gopLength);
    result += ", nrBFrames=" + toString(currentSettings_.gop_.nrBFrames);
    result += ", longTermRef=" + toString(currentSettings_.gop_.longTermRef);
    result += ", longTermFreq=" + toString(currentSettings_.gop_.longTermFreq);
    result += ", periodIDR=" + toString(currentSettings_.gop_.periodIDR);

    result += "\nProfile: profile=" + currentSettings_.profile_.profile;
    result += ", level=" + currentSettings_.profile_.level;
    result += ", tier=" + toString(currentSettings_.profile_.tier);

    result += "\nGMV: frameIndex=" + toString(currentSettings_.gmv_.frameIndex);
    result += ", gmVectorX=" + toString(currentSettings_.gmv_.gmVectorX);
    result += ", gmVectorY=" + toString(currentSettings_.gmv_.gmVectorY);

    return result;
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
