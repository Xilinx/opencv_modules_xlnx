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

#include "vcudeccontext.hpp"

#include <opencv2/core.hpp>

#include "vcudevice.hpp"
#include "vcuframe.hpp"
#include "vcurawout.hpp"
#include "vcureader.hpp"

extern "C" {
#include "config.h"
#include "lib_common/BufCommon.h"
#include "lib_common/BufferAPI.h"
#include "lib_common/BufferHandleMeta.h"
#include "lib_common/BufferSeiMeta.h"
#include "lib_common/Context.h"
#include "lib_common/DisplayInfoMeta.h"
#include "lib_common/Error.h"
#include "lib_common/FourCC.h"
#include "lib_common/PicFormat.h"
#include "lib_common/PixMapBuffer.h"
#include "lib_common/StreamBuffer.h"
#include "lib_common/BufferPictureDecMeta.h"

#include "lib_common_dec/DecBuffers.h"
#include "lib_common_dec/DecOutputSettings.h"

#include "lib_decode/DecSettings.h"
#include "lib_decode/lib_decode.h"


#include "lib_log/LoggerDefault.h"
}

#include "lib_app/BufPool.h"
#include "lib_app/PixMapBufPool.h"
#include "lib_app/timing.h"

#include <condition_variable>
#include <functional>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <thread>


extern "C" {
typedef struct AL_TAllocator AL_TAllocator;
typedef struct AL_TIpCtrl AL_TIpCtrl;
typedef struct AL_TDriver AL_TDriver;
typedef struct AL_IDecScheduler AL_IDecScheduler;
}


namespace cv {
namespace vcucodec {

class DecoderContext : public DecContext
{
public:
    DecoderContext(Config &config, AL_TAllocator *pAllocator, Ptr<RawOutput> rawOutput);
    ~DecoderContext();

    void start(WorkerConfig wCfg) override;
    void finish() override;

    bool running() const override { return running_; }
    bool eos() const override { return eos_; }

public: // used by static callback functions in this file
    void createBaseDecoder(Ptr<Device> device);
    AL_HDecoder getBaseDecoderHandle() const { return hBaseDec_; }
    AL_ERR setupBaseDecoderPool(int32_t iBufferNumber, AL_TStreamSettings const *pStreamSettings,
                                AL_TCropInfo const *pCropInfo);
    void receiveBaseDecoderDecodedFrame(AL_TBuffer *pFrame);
    void frameDone(Frame const &f);
    void manageError(AL_ERR eError);
    void receiveFrameToDisplayFrom(Ptr<Frame> pFrame);

private:

    bool waitExit(uint32_t uTimeout);
    int32_t getNumConcealedFrame() const { return iNumFrameConceal_; };
    int32_t getNumDecodedFrames() const { return iNumDecodedFrames_; };
    std::unique_lock<std::mutex> lockDisplay()
    {
        return std::unique_lock<std::mutex>(hDisplayMutex_);
    };
    void stopSendingBuffer()
    {
        lockDisplay();
        bPushBackToDecoder_ = false;
    };
    bool canSendBackBufferToDecoder() { return bPushBackToDecoder_; };

    AL_HANDLE getDecoderHandle() const;
    AL_ERR treatError(Ptr<Frame> pFrame);
    AL_TDimension computeBaseDecoderFinalResolution(AL_TStreamSettings const *pStreamSettings);
    int32_t computeBaseDecoderRecBufferSizing(AL_TStreamSettings const *pStreamSettings,
                                              AL_TDecOutputSettings const *pUserOutputSettings_);
    void attachMetaDataToBaseDecoderRecBuffer(AL_TStreamSettings const *pStreamSettings,
                                              AL_TBuffer *pDecPict);
    void ctrlswDecRun(WorkerConfig wCfg);

    bool running_;
    bool eos_;
    bool await_eos_;

    AL_TAllocator *pAllocator_;
    AL_HDecoder hBaseDec_ = nullptr;
    Ptr<RawOutput> rawOutput_{};
    bool bPushBackToDecoder_ = true;
    int32_t iNumFrameConceal_ = 0;
    int32_t iNumDecodedFrames_ = 0;
    AL_TDecCallBacks CB_{};
    AL_TDecSettings *pDecSettings_;
    bool bUsePreAlloc_ = false;
    PixMapBufPool baseBufPool_;
    AL_TDecOutputSettings *pUserOutputSettings_;
    std::ofstream seiOutput_;
    std::ofstream seiSyncOutput_;
    std::thread ctrlswThread_;
    std::map<AL_TBuffer *, std::vector<AL_TSeiMetaData *>> displaySeis_;
    EDecErrorLevel eExitCondition = DEC_ERROR;
    AL_EVENT hExitMain_ = nullptr;
    std::mutex hDisplayMutex_;
};

namespace { // anonymous

/* We need at least 1 buffer to copy the output on a file */
uint32_t constexpr uDefaultNumBuffersHeldByNextComponent = 1;

AL_TDecSettings getDefaultDecSettings(void)
{
    AL_TDecSettings settings{};
    AL_DecSettings_SetDefaults(&settings);
    settings.uNumBuffersHeldByNextComponent = uDefaultNumBuffersHeldByNextComponent;
    return settings;
}

AL_EFbStorageMode getMainOutputStorageMode(AL_TDecOutputSettings tUserOutputSettings,
                                           AL_EFbStorageMode eOutstorageMode)
{
    AL_EFbStorageMode eOutputStorageMode = eOutstorageMode;

    if (tUserOutputSettings.bCustomFormat)
    {
        if (tUserOutputSettings.tPicFormat.eStorageMode != AL_FB_MAX_ENUM)
            eOutputStorageMode = tUserOutputSettings.tPicFormat.eStorageMode;
        else
            eOutputStorageMode = AL_FB_RASTER;
    }

    return eOutputStorageMode;
}

/* duplicated from Utils.h as we can't take these from inside the libraries */
int32_t roundUp(int32_t iVal, int32_t iRnd) { return (iVal + iRnd - 1) / iRnd * iRnd; }

/* Update picture format using stream settings and decoder's settings*/
void setDecOutputSettings(AL_TDecOutputSettings &tUserOutputSettings,
                          AL_TStreamSettings const &tStreamSettings,
                          AL_TDecSettings const &tDecSettings)
{
    AL_TPicFormat &tPicFormat = tUserOutputSettings.tPicFormat;

    /* Chroma mode */
    if (AL_CHROMA_MAX_ENUM == tPicFormat.eChromaMode)
        tPicFormat.eChromaMode = tStreamSettings.eChroma;

    /* Bitdepth */

    bool bUserProvidedExplicitBitdepth = (tPicFormat.uBitDepth != (uint8_t)OUTPUT_BD_FIRST) &&
                                         (tPicFormat.uBitDepth != (uint8_t)OUTPUT_BD_ALLOC) &&
                                         (tPicFormat.uBitDepth != (uint8_t)OUTPUT_BD_STREAM);

    if (!bUserProvidedExplicitBitdepth)
        tPicFormat.uBitDepth = tStreamSettings.iBitDepth;

    /* Plane mode */
    if (AL_PLANE_MODE_MAX_ENUM == tPicFormat.ePlaneMode)
        tPicFormat.ePlaneMode = GetInternalBufPlaneMode(tPicFormat.eChromaMode);

    if (AL_COMPONENT_ORDER_MAX_ENUM == tPicFormat.eComponentOrder)
        tPicFormat.eComponentOrder = AL_COMPONENT_ORDER_YUV;

    tUserOutputSettings.tPicFormat.eStorageMode =
        getMainOutputStorageMode(tUserOutputSettings, tDecSettings.eFBStorageMode);

    if (IsTile(tUserOutputSettings.tPicFormat.eStorageMode))
        tUserOutputSettings.tPicFormat.eSamplePackMode = AL_SAMPLE_PACK_MODE_PACKED;

    if (tUserOutputSettings.tPicFormat.ePlaneMode == AL_PLANE_MODE_INTERLEAVED &&
        tUserOutputSettings.tPicFormat.eChromaMode == AL_CHROMA_4_4_4)
        tUserOutputSettings.tPicFormat.eAlphaMode = AL_ALPHA_MODE_AFTER;
}

std::string sequencePictureToString(AL_ESequenceMode sequencePicture)
{
    if (sequencePicture == AL_SM_UNKNOWN)
        return "unknown";

    if (sequencePicture == AL_SM_PROGRESSIVE)
        return "progressive";

    if (sequencePicture == AL_SM_INTERLACED)
        return "interlaced";
    return "max enum";
}

void showStreamInfo(int32_t BufferNumber, int32_t BufferSize,
                    AL_TStreamSettings const *pStreamSettings, AL_TCropInfo const *pCropInfo,
                    TFourCC tFourCC, AL_TDimension outputDim)
{
    int32_t iWidth = outputDim.iWidth;
    int32_t iHeight = outputDim.iHeight;

    std::stringstream ss;
    ss << "Resolution: " << iWidth << "x" << iHeight << std::endl;
    ss << "FourCC: " << AL_FourCCToString(tFourCC).cFourcc << std::endl;
    ss << "Profile: " << AL_GET_PROFILE_IDC(pStreamSettings->eProfile) << std::endl;
    int32_t iOutBitdepth = AL_GetBitDepth(tFourCC);

    if (pStreamSettings->iLevel != -1)
        ss << "Level: " << pStreamSettings->iLevel << std::endl;
    ss << "Bitdepth: " << iOutBitdepth << std::endl;

    if (AL_NeedsCropping(pCropInfo))
    {
        auto uCropWidth = pCropInfo->uCropOffsetLeft + pCropInfo->uCropOffsetRight;
        auto uCropHeight = pCropInfo->uCropOffsetTop + pCropInfo->uCropOffsetBottom;
        ss << "Crop top: " << pCropInfo->uCropOffsetTop << std::endl;
        ss << "Crop bottom: " << pCropInfo->uCropOffsetBottom << std::endl;
        ss << "Crop left: " << pCropInfo->uCropOffsetLeft << std::endl;
        ss << "Crop right: " << pCropInfo->uCropOffsetRight << std::endl;
        ss << "Display resolution: " << iWidth - uCropWidth << "x" << iHeight - uCropHeight
           << std::endl;
    }
    ss << "Sequence picture: " << sequencePictureToString(pStreamSettings->eSequenceMode)
       << std::endl;
    ss << "Buffers needed: " << BufferNumber << " of size " << BufferSize << std::endl;

    LogInfo(CC_DARK_BLUE, "%s\n", ss.str().c_str());
}

int32_t configureDecBufPool(PixMapBufPool &SrcBufPool, AL_TPicFormat const &tPicFormat,
                             AL_TDimension const &tDim, int32_t iPitchY,
                             bool bConfigurePlanarAndSemiplanar)
{
    auto const tFourCC = AL_GetFourCC(tPicFormat);
    SrcBufPool.SetFormat(tDim, tFourCC);

    std::vector<AL_TPlaneDescription> vPlaneDesc;
    int32_t iOffset = 0;

    AL_EPlaneId usedPlanes[AL_MAX_BUFFER_PLANES];
    int32_t iNbPlanes = AL_Plane_GetBufferPixelPlanes(tPicFormat, usedPlanes);

    // Set pixels planes
    // -----------------
    for (int32_t iPlane = 0; iPlane < iNbPlanes; iPlane++)
    {
        int32_t iPitch = (usedPlanes[iPlane] == AL_PLANE_Y || usedPlanes[iPlane] == AL_PLANE_YUV)
                             ? iPitchY
                             : AL_GetChromaPitch(tFourCC, iPitchY);
        vPlaneDesc.push_back(AL_TPlaneDescription{usedPlanes[iPlane], iOffset, iPitch});

        /* We ensure compatibility with 420/422. Only required when we use prealloc configured for
         * 444 chroma-mode (worst case) and the real chroma-mode is unknown. Breaks planes agnostic
         * allocation. */

        if (bConfigurePlanarAndSemiplanar && usedPlanes[iPlane] == AL_PLANE_U)
            vPlaneDesc.push_back(AL_TPlaneDescription{AL_PLANE_UV, iOffset, iPitch});

        iOffset += AL_DecGetAllocSize_Frame_PixPlane(&tPicFormat, tDim, iPitch, usedPlanes[iPlane]);
    }

    SrcBufPool.AddChunk(iOffset, vPlaneDesc);

    return iOffset;
}

void inputParsed(AL_TBuffer *pParsedFrame, void *pUserParam, int32_t iParsingId)
{
    (void)pParsedFrame;
    (void)pUserParam;
    (void)iParsingId;
}

static void frameDecoded(AL_TBuffer *pFrame, void *pUserParam)
{
    auto pCtx = static_cast<DecoderContext *>(pUserParam);
    pCtx->receiveBaseDecoderDecodedFrame(pFrame);
}

static void parsedSei(bool bIsPrefix, int32_t iPayloadType, uint8_t *pPayload,
                       int32_t iPayloadSize, void *pUserParam)
{
    (void)bIsPrefix;
    (void)iPayloadType;
    (void)pPayload;
    (void)iPayloadSize;
    (void)pUserParam;
}

static void decoderError(AL_ERR eError, void *pUserParam)
{
    auto pCtx = static_cast<DecoderContext *>(pUserParam);

    pCtx->manageError(eError);
}

static void baseDecoderFrameDisplay(AL_TBuffer *pFrame, AL_TInfoDecode *pInfo, void *pUserParam)
{
    bool isEos = !pFrame && !pInfo;
    bool release_only = pFrame && !pInfo;
    if (!release_only)
    {
        Ptr<Frame> frame;
        auto pCtx = reinterpret_cast<DecoderContext *>(pUserParam);
        if (!isEos)
        {
            frame = Frame::create(
                pFrame, pInfo,
                [pCtx](Frame const &f) { // Custom callback logic for when the frame is processed
                    pCtx->frameDone(f);
                });
            frame->invalidate();
        }
        pCtx->receiveFrameToDisplayFrom(frame);
    }
}

static AL_ERR baseResolutionFound(int32_t iBufferNumber, AL_TStreamSettings const *pStreamSettings,
                                   AL_TCropInfo const *pCropInfo, void *pUserParam)
{
    auto pCtx = (DecoderContext *)pUserParam;
    return pCtx->setupBaseDecoderPool(iBufferNumber, pStreamSettings, pCropInfo);
}

struct codec_error : public std::runtime_error
{
    explicit codec_error(AL_ERR eErrCode)
        : std::runtime_error(AL_Codec_ErrorToString(eErrCode)), Code(eErrCode)
    {
    }

    const AL_ERR Code;
};

void showStatistics(double durationInSeconds, int32_t iNumFrameConceal, int32_t decodedFrameNumber,
                    bool timeoutOccurred)
{
    std::string guard = "Decoded time = ";

    if (timeoutOccurred)
        guard = "TIMEOUT = ";

    auto msg = guard + "%.4f s;  Decoding FrameRate ~ %.4f Fps; Frame(s) conceal = %d\n";
    LogInfo(msg.c_str(), durationInSeconds, decodedFrameNumber / durationInSeconds,
            iNumFrameConceal);
}

void adjustStreamBufferSettings(Config &config)
{
    uint32_t uMinStreamBuf = config.tDecSettings.iStackSize;
    config.uInputBufferNum = max(uMinStreamBuf, config.uInputBufferNum);
    config.zInputBufferSize = max(size_t(1), config.zInputBufferSize);
}

void checkAndAdjustChannelConfiguration(Config &config)
{
    FILE *out = g_Verbosity ? stdout : nullptr;

    // Check base decoder settings
    // ---------------------------
    {
        int32_t err = AL_DecSettings_CheckValidity(&config.tDecSettings, out);
        err += AL_DecOutputSettings_CheckValidity(&config.tUserOutputSettings,
                                                  config.tDecSettings.eCodec, out);

        if (err)
        {
            std::stringstream ss;
            ss << err << " errors(s). " << "Invalid settings, please check the parameters.";
            CV_Error(cv::Error::StsBadArg, ss.str());
        }

        auto const incoherencies = AL_DecSettings_CheckCoherency(&config.tDecSettings, out);

        if (incoherencies < 0)
            CV_Error(cv::Error::StsBadArg,
                     "Fatal coherency error in settings, please check the parameters.");
    }

    // Adjust settings
    // ---------------
    adjustStreamBufferSettings(config);
}

void configureInputPool(Config const &config, AL_TAllocator *pAllocator, BufPool &tInputPool)
{
    std::string sDebugName = "input_pool";
    uint32_t uNumBuf = config.uInputBufferNum;
    uint32_t zBufSize = config.zInputBufferSize;
    AL_TMetaData *pBufMeta = nullptr;

    auto ret = tInputPool.Init(pAllocator, uNumBuf, zBufSize, pBufMeta, sDebugName);

    if (pBufMeta != nullptr)
        AL_MetaData_Destroy(pBufMeta);

    if (!ret)
        throw std::runtime_error("Can't create BufPool");
}


} // namespace anonymous

Config::Config()
{
    tDecSettings = getDefaultDecSettings();
}

DecoderContext::DecoderContext(Config &config, AL_TAllocator *pAlloc, Ptr<RawOutput> rawOutput)
    : rawOutput_(rawOutput)
{
    pAllocator_ = pAlloc;
    pDecSettings_ = &config.tDecSettings;
    pUserOutputSettings_ = &config.tUserOutputSettings;
    rawOutput_->configure(config.tOutputFourCC, config.iOutputBitDepth, config.iMaxFrames);
    running_ = false;
    eos_ = false;
    await_eos_ = false;
    eExitCondition = config.eExitCondition;
    hExitMain_ = Rtos_CreateEvent(false);
}

DecoderContext::~DecoderContext(void)
{
    await_eos_ = true;
    eos_ = true;
    if (ctrlswThread_.joinable())
        ctrlswThread_.join();
    Rtos_DeleteEvent(hExitMain_);
}

AL_HANDLE DecoderContext::getDecoderHandle() const
{
    AL_HANDLE h = hBaseDec_;
    return h;
}

bool DecoderContext::waitExit(uint32_t uTimeout) { return Rtos_WaitEvent(hExitMain_, uTimeout); }

AL_TDimension
DecoderContext::computeBaseDecoderFinalResolution(AL_TStreamSettings const *pStreamSettings)
{
    AL_TDimension tOutputDim = pStreamSettings->tDim;

    /* For pre-allocation, we must use 8x8 (HEVC) or MB (AVC) rounded dimensions, like the SPS. */
    /* Actually, round up to the LCU so we're able to support resolution changes with the same LCU
     * sizes. */
    /* And because we don't know the codec here, always use 64 as MB/LCU size. */
    tOutputDim.iWidth = roundUp(tOutputDim.iWidth, 64);
    tOutputDim.iHeight = roundUp(tOutputDim.iHeight, 64);

    return tOutputDim;
}

int32_t
DecoderContext::computeBaseDecoderRecBufferSizing(AL_TStreamSettings const *pStreamSettings,
                                                  AL_TDecOutputSettings const *pUserOutputSettings)
{
    // Up to this point pUserOutputSettings is already updated in the resolution found callback
    // (setupBaseDecoderPool)
    int32_t iBufferSize = 0;

    // Compute output resolution
    AL_TDimension tOutputDim = computeBaseDecoderFinalResolution(pStreamSettings);

    // Buffer sizing
    auto minPitch = AL_Decoder_GetMinPitch(tOutputDim.iWidth, &pUserOutputSettings->tPicFormat);

    bool bConfigurePlanarAndSemiplanar = bUsePreAlloc_;
    iBufferSize = configureDecBufPool(baseBufPool_, pUserOutputSettings->tPicFormat, tOutputDim,
                                      minPitch, bConfigurePlanarAndSemiplanar);

    return iBufferSize;
}

void DecoderContext::attachMetaDataToBaseDecoderRecBuffer(AL_TStreamSettings const *pStreamSettings,
                                                          AL_TBuffer *pDecPict)
{
    (void)pStreamSettings;

    AL_TPictureDecMetaData *pPictureDecMeta = AL_PictureDecMetaData_Create();
    AL_Buffer_AddMetaData(pDecPict, (AL_TMetaData *)pPictureDecMeta);

    AL_TDisplayInfoMetaData *pDisplayInfoMeta = AL_DisplayInfoMetaData_Create();
    AL_Buffer_AddMetaData(pDecPict, (AL_TMetaData *)pDisplayInfoMeta);
}

AL_ERR DecoderContext::setupBaseDecoderPool(int32_t iBufferNumber,
                                            AL_TStreamSettings const *pStreamSettings,
                                            AL_TCropInfo const *pCropInfo)
{
    auto lock = lockDisplay();

    setDecOutputSettings(*pUserOutputSettings_, *pStreamSettings, *pDecSettings_);

    if (!AL_Decoder_ConfigureOutputSettings(getBaseDecoderHandle(), pUserOutputSettings_))
        throw std::runtime_error("Could not configure the output settings");

    /* Compute buffer sizing */
    int32_t iBufferSize = computeBaseDecoderRecBufferSizing(pStreamSettings, pUserOutputSettings_);

    AL_TCropInfo pUserCropInfo = *pCropInfo;

    AL_TDimension outputDim = pStreamSettings->tDim;
    showStreamInfo(iBufferNumber, iBufferSize, pStreamSettings, &pUserCropInfo,
                   AL_GetFourCC(pUserOutputSettings_->tPicFormat), outputDim);

    if (baseBufPool_.IsInit())
        return AL_SUCCESS;

    /* Create the buffers */
    int32_t iNumBuf = iBufferNumber + uDefaultNumBuffersHeldByNextComponent;

    if (!baseBufPool_.Init(pAllocator_, iNumBuf, "decoded picture buffer"))
        return AL_ERR_NO_MEMORY;

    // Attach the metas + push to decoder
    // ----------------------------------
    for (int32_t i = 0; i < iNumBuf; ++i)
    {
        auto pDecPict = baseBufPool_.GetSharedBuffer(AL_EBufMode::AL_BUF_MODE_NONBLOCK);

        if (!pDecPict)
            throw std::runtime_error("pDecPict is null");

        AL_Buffer_Cleanup(pDecPict.get());

        attachMetaDataToBaseDecoderRecBuffer(pStreamSettings, pDecPict.get());
        bool const bAdded = AL_Decoder_PutDisplayPicture(getBaseDecoderHandle(), pDecPict.get());

        if (!bAdded)
            throw std::runtime_error("bAdded must be true");
    }

    return AL_SUCCESS;
}

void DecoderContext::receiveBaseDecoderDecodedFrame(AL_TBuffer *pFrame)
{
    (void)pFrame;
    if (getBaseDecoderHandle())
        iNumDecodedFrames_++;
}

void DecoderContext::createBaseDecoder(Ptr<Device> device)
{
    CB_.endParsingCB = {&inputParsed, this};
    CB_.endDecodingCB = {&frameDecoded, this};
    CB_.displayCB = {&baseDecoderFrameDisplay, this};
    CB_.resolutionFoundCB = {&baseResolutionFound, this};
    CB_.parsedSeiCB = {&parsedSei, this};
    CB_.errorCB = {&decoderError, this};

    auto ctx = device->getCtx();
    AL_ERR error = AL_Decoder_CreateWithCtx(&hBaseDec_, ctx, pAllocator_, pDecSettings_, &CB_);

    if (AL_IS_ERROR_CODE(error))
        throw codec_error(error);

    if (!hBaseDec_)
        throw std::runtime_error("Cannot create base decoder");
}

void DecoderContext::manageError(AL_ERR eError)
{
    if (AL_IS_ERROR_CODE(eError) || eExitCondition == DEC_WARNING)
        Rtos_SetEvent(hExitMain_);
}

void DecoderContext::start(WorkerConfig wCfg)
{
    ctrlswThread_ = std::thread(&DecoderContext::ctrlswDecRun, this, wCfg);
    ctrlswThread_.detach();
    running_ = true;
}

void DecoderContext::finish()
{
    await_eos_ = true;
    rawOutput_->flush();
    Rtos_SetEvent(hExitMain_);
}

void DecoderContext::receiveFrameToDisplayFrom(Ptr<Frame> pFrame)
{
    std::unique_lock<std::mutex> lock(hDisplayMutex_);

    bool bLastFrame = pFrame == nullptr || await_eos_;

    if (!bLastFrame)
    {
        auto err = treatError(pFrame);

        if (AL_IS_ERROR_CODE(err))
            bLastFrame = true;
        else
        {
            {
                bool bIsFrameMainDisplay;
                auto hDec = getDecoderHandle();
                int32_t iBitDepthAlloc = 8;

                iBitDepthAlloc = AL_Decoder_GetMaxBD(hDec);
                bool bDecoderExists = getBaseDecoderHandle() != NULL;
                rawOutput_->process(pFrame, iBitDepthAlloc, bIsFrameMainDisplay, bLastFrame,
                                         bDecoderExists);

                if (bIsFrameMainDisplay && canSendBackBufferToDecoder() && !bLastFrame)
                {
                    if (err == AL_WARN_CONCEAL_DETECT || err == AL_WARN_HW_CONCEAL_DETECT ||
                        err == AL_WARN_INVALID_ACCESS_UNIT_STRUCTURE)
                        iNumFrameConceal_++;
                }
            }
        }
    }
    if (bLastFrame)
    {
        await_eos_ = true;
        if (rawOutput_->idle())
        {
            eos_ = true;
        }
    }
}

void DecoderContext::frameDone(Frame const &frame)
{
    if (frame.isMainOutput() && canSendBackBufferToDecoder() && !await_eos_)
    {
        if (!AL_Decoder_PutDisplayPicture(getDecoderHandle(), frame.getBuffer()))
        {
            throw std::runtime_error("Failed to put display picture back to decoder");
        }
    }

    if (!eos_ && await_eos_ && rawOutput_->idle())
    {
        eos_ = true;
        Rtos_SetEvent(hExitMain_);
    }
}

AL_ERR DecoderContext::treatError(Ptr<Frame> frame)
{
    AL_TBuffer *pFrame = frame->getBuffer();
    bool bExitError = false;
    AL_ERR err = AL_SUCCESS;

    auto hDec = getDecoderHandle();

    if (hDec)
    {
        err = AL_Decoder_GetFrameError(hDec, pFrame);
        bExitError |= AL_IS_ERROR_CODE(err);
    }

    if (bExitError)
    {
        LogDimmedWarning("\n%s\n", AL_Codec_ErrorToString(err));

        if (err == AL_WARN_SEI_OVERFLOW)
            LogDimmedWarning(
                "\nDecoder has discarded some SEI while the SEI metadata buffer was too small\n");

        LogError("Error: %d\n", err);
    }

    return err;
}


void DecoderContext::ctrlswDecRun(WorkerConfig wCfg)
{
    auto &config = *wCfg.pConfig;
    AL_TAllocator *pAllocator = nullptr;

    pAllocator = wCfg.device->getAllocator();

    // Configure the stream buffer pool
    // --------------------------------
    // Note : Must be before scopeExit so that AL_Decoder_Destroy can be called
    // before the BufPool destroyer. Can it be done differently so that it is not dependant of this
    // order ?
    BufPool tInputPool;
    configureInputPool(config, pAllocator, tInputPool);

    // Insure destroying is done even after throwing
    // ---------------------------------------------
    auto scopeDecoder = scopeExit(
        [&]()
        {
            stopSendingBuffer(); // Prevent to push buffer to the decoder while destroying it
            AL_Decoder_Destroy(getBaseDecoderHandle());
        });

    // Start feeding the decoder
    // -------------------------
    auto const uBegin = GetPerfTime();
    bool timeoutOccurred = false;

    {
        tInputPool.Commit();

        // Setup the reader of bitstream in the file.
        // It will send bitstream chunk to the decoder

        auto reader = Reader::createReader(getBaseDecoderHandle(), tInputPool);
        if (!reader->setPath(config.sIn))
        {
            CV_Error(cv::Error::StsBadArg, "Failed to set input file path");
        }
        reader->start();

        auto const maxWait = config.iTimeoutInSeconds * 1000;
        auto const timeout = maxWait >= 0 ? maxWait : AL_WAIT_FOREVER;

        if (!waitExit(timeout))
            timeoutOccurred = true;

        tInputPool.Decommit();
    }

    auto const uEnd = GetPerfTime();

    // Prevent the display to produce some outputs
    auto lock = lockDisplay();

    // Get the errors
    // --------------
    AL_ERR eErr = AL_SUCCESS;

    if (getBaseDecoderHandle())
        eErr = AL_Decoder_GetLastError(getBaseDecoderHandle());

    if (AL_IS_ERROR_CODE(eErr) ||
        (AL_IS_WARNING_CODE(eErr) && config.eExitCondition == DEC_WARNING))
    {
        throw codec_error(eErr);
    }

    if (AL_IS_WARNING_CODE(eErr))
        std::cerr << std::endl << "Warning: " << AL_Codec_ErrorToString(eErr) << std::endl;

    if (!getNumDecodedFrames())
        throw std::runtime_error("No frame decoded");

    auto const duration = (uEnd - uBegin) / 1000.0;
    showStatistics(duration, getNumConcealedFrame(), getNumDecodedFrames(), timeoutOccurred);
    eos_ = true;
}

/*static*/ std::shared_ptr<DecContext>
DecContext::create(std::shared_ptr<Config> pDecConfig, Ptr<RawOutput> rawOutput, WorkerConfig &wCfg)
{
    std::shared_ptr<DecoderContext> pDecodeCtx;
    std::set<std::string> const sDecDefaultDevicePath(DECODER_DEVICES);
    SetDefaultDecOutputSettings(&pDecConfig->tUserOutputSettings);
    pDecConfig->sDecDevicePath = sDecDefaultDevicePath;

    pDecConfig->tUserOutputSettings.tPicFormat.eStorageMode = AL_FB_RASTER;
    pDecConfig->tUserOutputSettings.bCustomFormat = true;

    // Setup of the decoder(s) architecture
    AL_Lib_Decoder_Init(AL_LIB_DECODER_ARCH_RISCV);

    // Create the device
    Ptr<Device> device;

    try
    {
        device = Device::create();
    }
    catch (const std::exception &e)
    {
        CV_Error(cv::Error::StsError, e.what());
    }

    auto &config = *pDecConfig;
    AL_TAllocator *pAllocator = nullptr;

    pAllocator = device->getAllocator();

    // Settings checks
    // ------------------
    checkAndAdjustChannelConfiguration(config);

    // Configure the decoders
    // ----------------------
    pDecodeCtx = std::shared_ptr<DecoderContext>(new DecoderContext(config, pAllocator, rawOutput));

    wCfg.pConfig = pDecConfig;
    wCfg.device = device;

    // Create the decoders
    // -------------------
    pDecodeCtx->createBaseDecoder(device);

    // Parametrization of the base decoder for traces
    // ----------------------------------------------
    auto hDec = pDecodeCtx->getBaseDecoderHandle();
    AL_Decoder_SetParam(hDec, "Fpga", config.iTraceIdx, config.iTraceNumber,
                        config.ipCtrlMode == AL_EIpCtrlMode::AL_IPCTRL_MODE_TRACE);

    // Parametrization of the lcevc decoder for traces
    // -----------------------------------------------
    return pDecodeCtx;
}

} // namespace cv
} // namespace vcucodec
