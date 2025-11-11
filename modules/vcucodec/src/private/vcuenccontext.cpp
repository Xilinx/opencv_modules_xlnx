// Modifications Copyright(C) [2025] Advanced Micro Devices, Inc. All rights reserved)

// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT
#if defined(HAVE_VCU_CTRLSW) || defined(HAVE_VCU2_CTRLSW)

//#include "ctrlsw_enc.hpp"
#include "config.h"

#include "lib_app/AL_RasterConvert.hpp"
#define HAS_COMPIL_FLAGS 0
#include "lib_app/BuildInfo.hpp"
#include "lib_app/convert.hpp"
#include "lib_app/FileUtils.hpp"
#include "lib_app/PixMapBufPool.hpp"
#include "lib_app/plateform.hpp"
#include "lib_app/SinkFrame.hpp"
#include "lib_app/timing.hpp"
#include "lib_app/UnCompFrameReader.hpp"
#include "lib_app/YuvIO.hpp"

#ifdef HAVE_VCU2_CTRLSW
#include "lib_app/CompFrameReader.hpp"
#include "lib_app/CompFrameCommon.hpp"
#endif

extern "C" {
#include "lib_common/BufferPictureMeta.h"
#include "lib_common/BufferStreamMeta.h"
#include "lib_common/Round.h"
#include "lib_common/StreamBuffer.h"
#include "lib_common_enc/EncBuffers.h"
#include "lib_common_enc/IpEncFourCC.h"
#include "lib_common_enc/RateCtrlMeta.h"
#include "lib_encode/lib_encoder.h"
}

#include "vcudata.hpp"
#include "vcudevice.hpp"
#include "vcuenccontext.hpp"
#include "vcuutils.hpp"
#include "vcuframe.hpp"

#include <condition_variable>
#include <iostream>
#include <regex>

namespace cv {
namespace vcucodec {

#define NUM_PASS_OUTPUT 1

#define MAX_NUM_REC_OUTPUT (MAX_NUM_LAYER > NUM_PASS_OUTPUT ? MAX_NUM_LAYER : NUM_PASS_OUTPUT)
#define MAX_NUM_BITSTREAM_OUTPUT NUM_PASS_OUTPUT


namespace { // anonymous

using DataCallback = std::function<void (std::vector<std::string_view>&)>;
using ChangeSourceCallback = std::function<void(int, int)>;


static std::string PictTypeToString(AL_ESliceType type)
{
    std::map<AL_ESliceType, std::string> m =
    {
        { AL_SLICE_B, "B" },
        { AL_SLICE_P, "P" },
        { AL_SLICE_I, "I" },
        { AL_SLICE_GOLDEN, "Golden" },
        { AL_SLICE_CONCEAL, "Conceal" },
        { AL_SLICE_SKIP, "Skip" },
        { AL_SLICE_REPEAT, "Repeat" },
    };

    return m.at(type);
}

struct RCPlugin
{
    uint32_t capacity;
    uint32_t qpFifo[32];
    uint32_t head;
    uint32_t tail;
    uint32_t curQp;
};

void RCPlugin_SetNextFrameQP(AL_TEncSettings const* pSettings,
                                            AL_TAllocator* pDmaAllocator)
{
    if (pSettings->hRcPluginDmaContext == NULL)
        throw std::runtime_error("RC Context isn't allocated");

    auto rc = (RCPlugin*)AL_Allocator_GetVirtualAddr(pDmaAllocator,
                                                     pSettings->hRcPluginDmaContext);

    if (rc == NULL)
        throw std::runtime_error("RC Context isn't correctly defined");

    rc->qpFifo[rc->head] = rc->curQp;
    rc->head = (rc->head + 1) % rc->capacity;

    ++rc->curQp;

    if (rc->curQp > 51)
        rc->curQp = 30;
}

void RCPlugin_Init(AL_TEncSettings* pSettings, AL_TEncChanParam* pChParam,
                                 AL_TAllocator* pDmaAllocator)
{
    pSettings->hRcPluginDmaContext = NULL;
    pChParam->pRcPluginDmaContext = 0;
    pChParam->zRcPluginDmaSize = 0;

    if (pChParam->tRCParam.eRCMode == AL_RC_PLUGIN)
    {
        pChParam->zRcPluginDmaSize = sizeof(struct RCPlugin);
        pSettings->hRcPluginDmaContext = AL_Allocator_Alloc(pDmaAllocator,
                                                             pChParam->zRcPluginDmaSize);

        if (pSettings->hRcPluginDmaContext == NULL)
            throw std::runtime_error("Couldn't allocate RC Plugin Context");

        auto rc = (RCPlugin*)AL_Allocator_GetVirtualAddr(pDmaAllocator,
                                                         pSettings->hRcPluginDmaContext);
        rc->head = 0;
        rc->tail = 0;
        rc->capacity = 32;
        rc->curQp = 30;

        for (uint32_t i = 0; i < rc->capacity; ++i)
            rc->qpFifo[i] = 0;
    }
}


struct EncoderSink
{
#ifdef HAVE_VCU2_CTRLSW
    explicit EncoderSink(EncContext::Config const& cfg, AL_RiscV_Ctx ctx,
                         AL_TAllocator* pAllocator) :
        m_cfg(cfg),
        pAllocator{pAllocator},
        pSettings{&cfg.Settings}
    {
        assert(ctx);
        AL_CB_EndEncoding onEncoding = { &EncoderSink::EndEncoding, this };

        AL_ERR errorCode = AL_Encoder_CreateWithCtx(&hEnc, ctx, this->pAllocator,
                                                     &cfg.Settings, onEncoding);

        if (AL_IS_ERROR_CODE(errorCode))
            throw en_codec_error(AL_Codec_ErrorToString(errorCode), errorCode);

        if (AL_IS_WARNING_CODE(errorCode))
            std::cout << AL_Codec_ErrorToString(errorCode) << std::endl;

        for (int32_t i = 0; i < MAX_NUM_REC_OUTPUT; ++i)
            RecOutput[i].reset(new NullFrameSink);

        for (int32_t i = 0; i < MAX_NUM_LAYER; i++)
            m_input_picCount[i] = 0;

        m_pictureType = cfg.RunInfo.printPictureType ? AL_SLICE_MAX_ENUM : -1;

        iPendingStreamCnt = 1;
    }
#endif

    explicit EncoderSink(EncContext::Config const& cfg, AL_IEncScheduler* pScheduler,
                         AL_TAllocator* pAllocator) :
        m_cfg(cfg),
        pAllocator{pAllocator},
        pSettings{&cfg.Settings}
    {
        AL_CB_EndEncoding onEncoding = { &EncoderSink::EndEncoding, this };

        AL_ERR errorCode = AL_Encoder_Create(&hEnc, pScheduler, this->pAllocator,
                                             &cfg.Settings, onEncoding);

        if (AL_IS_ERROR_CODE(errorCode))
            throw en_codec_error(AL_Codec_ErrorToString(errorCode), errorCode);

        if (AL_IS_WARNING_CODE(errorCode))
            std::cout << AL_Codec_ErrorToString(errorCode) << std::endl;

        for (int32_t i = 0; i < MAX_NUM_REC_OUTPUT; ++i)
            RecOutput[i].reset(new NullFrameSink);

        for (int32_t i = 0; i < MAX_NUM_LAYER; i++)
            m_input_picCount[i] = 0;

        m_pictureType = cfg.RunInfo.printPictureType ? AL_SLICE_MAX_ENUM : -1;

        // TODO: AL_Encoder_SetHDRSEIs(hEnc, &tHDRSEIs);

        iPendingStreamCnt = 1;
    }

    ~EncoderSink(void)
    {
        AL_Encoder_Destroy(hEnc);
    }

    void SetChangeSourceCallback(ChangeSourceCallback changeSourceCB)
    {
        m_changeSourceCB = changeSourceCB;
    }

    bool waitForCompletion(void)
    {
        std::unique_lock<std::mutex> lock(encoding_complete_mutex);
        return encoding_complete_cv.wait_for (lock, std::chrono::seconds(1),
                                             [this] { return encoding_finished; });
    }

    // Synchronization for blocking eos()
    std::mutex encoding_complete_mutex;
    std::condition_variable encoding_complete_cv;
    bool encoding_finished = false;

    void PreprocessFrame()
    {
    }

    void ProcessFrame(AL_TBuffer* Src)
    {
        if (m_input_picCount[0] == 0)
            m_StartTime = GetPerfTime();

        if (!Src)
        {
            if (!AL_Encoder_Process(hEnc, nullptr, nullptr))
                CheckErrorAndThrow();
            return;
        }

        fflush(stdout);

        CheckSourceResolutionChanged(Src);

        if (pSettings->hRcPluginDmaContext != NULL)
            RCPlugin_SetNextFrameQP(pSettings, this->pAllocator);

        if (!AL_Encoder_Process(hEnc, Src, nullptr))
            CheckErrorAndThrow();

        m_input_picCount[0]++;
    }

    AL_ERR GetLastError(void)
    {
        return m_EncoderLastError;
    }
    int fps() {return fps_;}
    int nrFrames() {return m_input_picCount[0];}

    std::unique_ptr<IFrameSink> RecOutput[MAX_NUM_REC_OUTPUT];
    DataCallback dataCallback_;
    AL_HEncoder hEnc;
    bool shouldAddDummySei = false;

private:
    int32_t iPendingStreamCnt;
    int32_t m_input_picCount[MAX_NUM_LAYER] {};
    int32_t m_pictureType = -1;
    uint64_t m_StartTime = 0;
    uint64_t m_EndTime = 0;
    int fps_ = 0;
    EncContext::Config const& m_cfg;

    AL_TAllocator* pAllocator;
    AL_TEncSettings const* pSettings;
    ChangeSourceCallback m_changeSourceCB;
    AL_TDimension tLastEncodedDim;
    AL_ERR m_EncoderLastError = AL_SUCCESS;

    void CheckErrorAndThrow(void)
    {
        AL_ERR eErr = AL_Encoder_GetLastError(hEnc);
        throw std::runtime_error(AL_IS_ERROR_CODE(eErr) ? AL_Codec_ErrorToString(eErr) : "Failed");
    }

    static bool isStreamReleased(AL_TBuffer* pStream, AL_TBuffer const* pSrc)
    {
        return pStream && !pSrc;
    }

    static bool isSourceReleased(AL_TBuffer* pStream, AL_TBuffer const* pSrc)
    {
        return !pStream && pSrc;
    }

    static void EndEncoding(void* userParam, AL_TBuffer* pStream, AL_TBuffer const* pSrc, int)
    {
        auto pThis = (EncoderSink*)userParam;

        if (isStreamReleased(pStream, pSrc) || isSourceReleased(pStream, pSrc))
            return;

        Ptr<Data> data = Data::create(pStream, pThis->hEnc);
        pThis->processOutput(data);
    }

    void ComputeQualityMeasure(AL_TRateCtrlMetaData* pMeta)
    {
        if (!pMeta->bFilled)
            return;
    }

    void AddSei(AL_TBuffer* pStream, bool isPrefix, int32_t payloadType, uint8_t* payload,
                int32_t payloadSize, int32_t tempId)
    {
        int32_t seiSection = AL_Encoder_AddSei(hEnc, pStream, isPrefix, payloadType,
                                               payload, payloadSize, tempId);

        if (seiSection < 0)
            std::cout << "Failed to add dummy SEI (id:" << seiSection << ")" << std::endl;
    }

    AL_ERR PreprocessOutput(Ptr<Data> pStream)
    {
        AL_ERR eErr = AL_Encoder_GetLastError(hEnc);

        if (AL_IS_ERROR_CODE(eErr))
        {
            LogError("%s\n", AL_Codec_ErrorToString(eErr));
            m_EncoderLastError = eErr;
        }

        if (AL_IS_WARNING_CODE(eErr))
            std::cout << AL_Codec_ErrorToString(eErr) << std::endl;

        if (pStream->buf() && shouldAddDummySei)
        {
            constexpr int32_t payloadSize = 8 * 10;
            uint8_t payload[payloadSize];

            for (int32_t i = 0; i < payloadSize; ++i)
                payload[i] = i;

            AL_TStreamMetaData* pStreamMeta = (AL_TStreamMetaData*)AL_Buffer_GetMetaData(
                pStream->buf(), AL_META_TYPE_STREAM);
            AddSei(pStream->buf(), false, 15, payload, payloadSize, pStreamMeta->uTemporalID);
            AddSei(pStream->buf(), true, 18, payload, payloadSize, pStreamMeta->uTemporalID);
        }

        if (pStream->buf() == EndOfStream)
            iPendingStreamCnt--;
        else
        {
            if (m_pictureType != -1)
            {
                auto const pMeta = (AL_TPictureMetaData*)AL_Buffer_GetMetaData(
                    pStream->buf(), AL_META_TYPE_PICTURE);
                m_pictureType = pMeta->eType;
                LogInfo("Picture Type %s (%i) %s\n", PictTypeToString(pMeta->eType).c_str(),
                        m_pictureType, pMeta->bSkipped ? "is skipped" : "");
            }

            AL_TRateCtrlMetaData* pMeta = (AL_TRateCtrlMetaData*)AL_Buffer_GetMetaData(
                pStream->buf(), AL_META_TYPE_RATECTRL);

            if (pMeta && pMeta->bFilled)
            {
            }
            std::vector<std::string_view> vec;
            pStream->walkBuffers([&vec](size_t size, uint8_t* data) {
                vec.push_back({(char*)data, size});
            });
            dataCallback_(vec);
        }

        return AL_SUCCESS;
    }

    void CloseOutputs(void)
    {
        m_EndTime = GetPerfTime();
        uint64_t timeDiff = m_EndTime - m_StartTime;
        if (timeDiff > 0) {
            fps_ = static_cast<int>((m_input_picCount[0] * 1000.0) / timeDiff);
        } else {
            fps_ = 0; // Avoid division by zero
        }
        // Signal that encoding is complete
        {
            std::lock_guard<std::mutex> lock(encoding_complete_mutex);
            encoding_finished = true;
        }
        encoding_complete_cv.notify_all();
    }

    void CheckAndAllocateConversionBuffer(TFourCC tConvFourCC, AL_TDimension const& tConvDim,
                                          std::shared_ptr<AL_TBuffer>& pConvYUV)
    {
        if (pConvYUV != nullptr)
        {
            AL_TDimension tCurrentConvDim = AL_PixMapBuffer_GetDimension(pConvYUV.get());

            if (tCurrentConvDim.iHeight >= tConvDim.iHeight &&
               tCurrentConvDim.iWidth >= tConvDim.iWidth)
                return;
        }

        AL_TBuffer* pYuv = AllocateDefaultYuvIOBuffer(tConvDim, tConvFourCC);

        if (pYuv == nullptr)
            throw std::runtime_error("Couldn't allocate reconstruct conversion buffer");

        pConvYUV = std::shared_ptr<AL_TBuffer>(pYuv, &AL_Buffer_Destroy);
    }

    void RecToYuv(AL_TBuffer const* pRec, AL_TBuffer* pYuv, TFourCC tYuvFourCC)
    {
        TFourCC tRecFourCC = AL_PixMapBuffer_GetFourCC(pRec);
        tConvFourCCFunc pFunc = GetConvFourCCFunc(tRecFourCC, tYuvFourCC);

        AL_PixMapBuffer_SetDimension(pYuv, AL_PixMapBuffer_GetDimension(pRec));

        if (!pFunc)
            throw std::runtime_error("Can't find a conversion function suitable for format");

        if (AL_IsTiled(tRecFourCC) == false)
            throw std::runtime_error("FourCC must be in Tile mode");
        return pFunc(pRec, pYuv);
    }

    void processOutput(Ptr<Data> pStream)
    {
        AL_ERR eErr;
        {
            eErr = PreprocessOutput(pStream);
        }

        if (AL_IS_ERROR_CODE(eErr))
        {
            LogError("%s\n", AL_Codec_ErrorToString(eErr));
            m_EncoderLastError = eErr;
        }

        if (AL_IS_WARNING_CODE(eErr))
            std::cout << AL_Codec_ErrorToString(eErr) << std::endl;

        AL_TRecPic RecPic;

        while(AL_Encoder_GetRecPicture(hEnc, &RecPic))
        {
            auto buf = RecPic.pBuf;
            int32_t iRecId = 0;

            if (buf)
            {
                TFourCC tFileRecFourCC = m_cfg.RecFourCC;
                AL_Buffer_InvalidateMemory(buf);

                TFourCC fourCC = AL_PixMapBuffer_GetFourCC(buf);

                if (AL_IsCompressed(fourCC))
                    RecOutput[iRecId]->ProcessFrame(buf);
                else
                {
                    if (AL_PixMapBuffer_GetFourCC(buf) != tFileRecFourCC)
                    {
                        std::shared_ptr<AL_TBuffer> bufPostConv;
                        CheckAndAllocateConversionBuffer(tFileRecFourCC,
                                                         AL_PixMapBuffer_GetDimension(buf),
                                                         bufPostConv);
                        RecToYuv(buf, bufPostConv.get(), tFileRecFourCC);
                        RecOutput[iRecId]->ProcessFrame(bufPostConv.get());
                    }
                    else
                        RecOutput[iRecId]->ProcessFrame(buf);
                }
            }
            AL_Encoder_ReleaseRecPicture(hEnc, &RecPic);
        }

        if (iPendingStreamCnt == 0)
            CloseOutputs();
    }

    void RequestSourceChange(int32_t iInputIdx, int32_t iLayerIdx)
    {
        if (m_changeSourceCB)
            m_changeSourceCB(iInputIdx, iLayerIdx);
    }

    void CheckSourceResolutionChanged(AL_TBuffer* pSrc)
    {
        (void)pSrc;
        AL_TDimension tNewDim = AL_PixMapBuffer_GetDimension(pSrc);
        bool bDimensionChanged = tNewDim.iWidth != tLastEncodedDim.iWidth ||
                                 tNewDim.iHeight != tLastEncodedDim.iHeight;

        if (bDimensionChanged)
        {
            AL_Encoder_SetInputResolution(hEnc, tNewDim);
            tLastEncodedDim = tNewDim;
        }
    }
};

using Config = EncContext::Config;
/*****************************************************************************/

struct SrcConverterParams
{
    AL_TDimension tDim;
    TFourCC tFileFourCC;
    AL_TPicFormat tSrcPicFmt;
    AL_ESrcFormat eSrcFormat;
};

/*****************************************************************************/
struct SrcBufChunk
{
    int32_t iChunkSize;
    std::vector<AL_TPlaneDescription> vPlaneDesc;
};

struct SrcBufDesc
{
    TFourCC tFourCC;
    std::vector<SrcBufChunk> vChunks;
};

/*****************************************************************************/
struct LayerResources
{
    void Init(Config& cfg, AL_TEncoderInfo tEncInfo, int32_t iLayerID,
              AL_TAllocator* pAllocator, int32_t chanId);

    void PushResources(Config& cfg, EncoderSink* enc);

    void OpenEncoderInput(Config& cfg, AL_HEncoder hEnc);

    bool SendInput(Config& cfg, EncoderSink* firstSink, void* pTraceHook);

    bool sendInputFileTo(std::unique_ptr<FrameReader>& frameReader, PixMapBufPool& SrcBufPool,
                         AL_TBuffer* Yuv, Config const& cfg, AL_TYUVFileInfo& FileInfo,
                         IConvSrc* pSrcConv, EncoderSink* pEncoderSink, int& iPictCount,
                         int& iReadCount);

    std::unique_ptr<FrameReader> InitializeFrameReader(Config& cfg, std::ifstream& YuvFile,
                                                       std::string sYuvFileName,
                                                       std::ifstream& MapFile,
                                                       std::string sMapFileName,
                                                       AL_TYUVFileInfo& FileInfo);

    void ChangeInput(Config& cfg, int32_t iInputIdx, AL_HEncoder hEnc);

    BufPool StreamBufPool;
    PixMapBufPool SrcBufPool;

    // Input/Output Format conversion
    std::ifstream YuvFile;
    std::ifstream MapFile;
    std::unique_ptr<FrameReader> frameReader;
    std::unique_ptr<IConvSrc> pSrcConv;
    std::shared_ptr<AL_TBuffer> SrcYuv;

    std::vector<uint8_t> RecYuvBuffer;
    std::unique_ptr<IFrameSink> frameWriter;

    int32_t iPictCount = 0;
    int32_t iReadCount = 0;

    int32_t iLayerID = 0;
    int32_t iInputIdx = 0;
    std::vector<ConfigYUVInput> layerInputs;
};

int32_t g_StrideHeight = -1;
int32_t g_Stride = -1;
int32_t constexpr g_defaultMinBuffers = 2;
bool g_MultiChunk = false;

void ValidateConfig(Config& cfg)
{
    std::string const invalid_settings("Invalid settings, check the [SETTINGS] section of your "
                                       "configuration file or check your commandline "
                                       "(use -h to get help)");

    if (cfg.MainInput.YUVFileName.empty())
        throw std::runtime_error("No YUV input was given, specify it in the [INPUT] section of "
                                 "your configuration file or in your commandline "
                                 "(use -h to get help)");

    FILE* out = stdout;

    if (!g_Verbosity)
        out = nullptr;

    auto const MaxLayer = cfg.Settings.NumLayer - 1;

    for (int32_t i = 0; i < cfg.Settings.NumLayer; ++i)
    {
        auto const err = AL_Settings_CheckValidity(&cfg.Settings, &cfg.Settings.tChParam[i], out);

        if (err != 0)
        {
            std::stringstream ss;
            ss << "Found: " << err << " errors(s). " << invalid_settings;
            throw std::runtime_error(ss.str());
        }

        auto const incoherencies = AL_Settings_CheckCoherency(&cfg.Settings,
                &cfg.Settings.tChParam[i], cfg.MainInput.FileInfo.FourCC, out);

        if (incoherencies < 0)
            throw std::runtime_error("Fatal coherency error in settings (layer[" +
                                     std::to_string(i) + "/" + std::to_string(MaxLayer) + "])");
    }
}

/*****************************************************************************/
std::shared_ptr<AL_TBuffer> AllocateConversionBuffer(int32_t iWidth, int32_t iHeight,
                                                            TFourCC tFourCC)
{
    AL_TBuffer* pYuv = AllocateDefaultYuvIOBuffer(AL_TDimension { iWidth, iHeight }, tFourCC);

    if (pYuv == nullptr)
        return nullptr;
    return std::shared_ptr<AL_TBuffer>(pYuv, &AL_Buffer_Destroy);
}

bool ReadSourceFrameBuffer(AL_TBuffer* pBuffer, AL_TBuffer* conversionBuffer,
        std::unique_ptr<FrameReader> const& frameReader, AL_TDimension tUpdatedDim, IConvSrc* hConv)
{

    AL_PixMapBuffer_SetDimension(pBuffer, tUpdatedDim);

    if (hConv)
    {
        AL_PixMapBuffer_SetDimension(conversionBuffer, tUpdatedDim);

        if (!frameReader->ReadFrame(conversionBuffer))
            return false;
        hConv->ConvertSrcBuf(conversionBuffer, pBuffer);
    }
    else
        return frameReader->ReadFrame(pBuffer);

    return true;
}

std::shared_ptr<AL_TBuffer> ReadSourceFrame(BaseBufPool* pBufPool,
    AL_TBuffer* conversionBuffer, std::unique_ptr<FrameReader> const& frameReader,
    AL_TDimension tUpdatedDim, IConvSrc* hConv)
{
    std::shared_ptr<AL_TBuffer> sourceBuffer = pBufPool->GetSharedBuffer();

    if (sourceBuffer == nullptr)
        throw std::runtime_error("sourceBuffer must exist");

    if (!ReadSourceFrameBuffer(sourceBuffer.get(), conversionBuffer, frameReader, tUpdatedDim, hConv))
        return nullptr;
    return sourceBuffer;
}

AL_TPicFormat GetSrcPicFormat(AL_TEncChanParam const& tChParam)
{
    AL_ESrcMode eSrcMode = tChParam.eSrcMode;
    auto eChromaMode = AL_GET_CHROMA_MODE(tChParam.ePicFormat);

    return AL_EncGetSrcPicFormat(eChromaMode, tChParam.uSrcBitDepth, eSrcMode);
}

bool IsConversionNeeded(SrcConverterParams& tSrcConverterParams)
{
    const TFourCC tSrcFourCC = AL_GetFourCC(tSrcConverterParams.tSrcPicFmt);

     if (tSrcConverterParams.tFileFourCC != tSrcFourCC)
    {
        if (AL_IsCompatible(tSrcConverterParams.tFileFourCC, tSrcFourCC))
            // Update PicFormat to avoid conversion
            AL_GetPicFormat(tSrcConverterParams.tFileFourCC, &tSrcConverterParams.tSrcPicFmt);
        else
        return true;
    }

    return false;
}

std::unique_ptr<IConvSrc> AllocateSrcConverter(SrcConverterParams const& tSrcConverterParams,
                                                      std::shared_ptr<AL_TBuffer>& pFileReaderYuv)
{
    // ********** Allocate the YUV buffer to read in the file **********
    pFileReaderYuv = AllocateConversionBuffer(tSrcConverterParams.tDim.iWidth,
        tSrcConverterParams.tDim.iHeight, tSrcConverterParams.tFileFourCC);

    if (pFileReaderYuv == nullptr)
        throw std::runtime_error("Couldn't allocate source conversion buffer");

    // ************* Allocate the YUV converter *************
    TFrameInfo tSrcFrameInfo = { tSrcConverterParams.tDim, tSrcConverterParams.tSrcPicFmt.uBitDepth,
                                 tSrcConverterParams.tSrcPicFmt.eChromaMode };
    (void)tSrcFrameInfo;

    switch(tSrcConverterParams.eSrcFormat)
    {
    case AL_SRC_FORMAT_RASTER:
        return std::make_unique<CYuvSrcConv>(tSrcFrameInfo);
#ifdef HAVE_VCU2_CTRLSW
    case AL_SRC_FORMAT_RASTER_MSB:
        return std::make_unique<CYuvSrcConv>(tSrcFrameInfo);
    case AL_SRC_FORMAT_TILE_64x4:
    case AL_SRC_FORMAT_TILE_32x4:
        return std::make_unique<CYuvSrcConv>(tSrcFrameInfo);
#endif
    default:
        throw std::runtime_error("Unsupported source conversion.");
    }

    return nullptr;
}

int32_t ComputeYPitch(int32_t iWidth, const AL_TPicFormat& tPicFormat)
{
    auto iPitch = AL_EncGetMinPitch(iWidth, &tPicFormat);

    if (g_Stride != -1)
    {
        if (g_Stride < iPitch)
            throw std::runtime_error("g_Stride(" + std::to_string(g_Stride) +
                ") must be higher or equal than iPitch(" + std::to_string(iPitch) + ")");
        iPitch = g_Stride;
    }
    return iPitch;
}

bool isLastPict(int32_t iPictCount, int32_t iMaxPict)
{
    return (iPictCount >= iMaxPict) && (iMaxPict != -1);
}

std::shared_ptr<AL_TBuffer> GetSrcFrame(int& iReadCount, int32_t iPictCount,
    std::unique_ptr<FrameReader> const& frameReader, AL_TYUVFileInfo const& FileInfo,
    PixMapBufPool& SrcBufPool, AL_TBuffer* Yuv, AL_TEncChanParam const& tChParam,
    Config const& cfg, IConvSrc* pSrcConv)
{
    std::shared_ptr<AL_TBuffer> frame;

    if (!isLastPict(iPictCount, cfg.RunInfo.iMaxPict))
    {
        if (cfg.MainInput.FileInfo.FrameRate != tChParam.tRCParam.uFrameRate)
        {
            iReadCount += frameReader->GotoNextPicture(FileInfo.FrameRate,
                tChParam.tRCParam.uFrameRate, iPictCount, iReadCount);
        }

        auto tUpdatedDim = AL_TDimension {AL_GetSrcWidth(tChParam), AL_GetSrcHeight(tChParam)};
        frame = ReadSourceFrame(&SrcBufPool, Yuv, frameReader, tUpdatedDim, pSrcConv);

        iReadCount++;
    }
    return frame;
}

AL_ESrcMode SrcFormatToSrcMode(AL_ESrcFormat eSrcFormat)
{
    switch(eSrcFormat)
    {
    case AL_SRC_FORMAT_RASTER:
        return AL_SRC_RASTER;
#ifdef HAVE_VCU2_CTRLSW
    case AL_SRC_FORMAT_RASTER_MSB:
        return AL_SRC_RASTER_MSB;
    case AL_SRC_FORMAT_TILE_64x4:
        return AL_SRC_TILE_64x4;
    case AL_SRC_FORMAT_TILE_32x4:
        return AL_SRC_TILE_32x4;
#endif
    default:
        throw std::runtime_error("Unsupported source format.");
    }
}

/*****************************************************************************/
SrcBufDesc GetSrcBufDescription(AL_TDimension tDimension, uint8_t uBitDepth,
        AL_EChromaMode eCMode, AL_ESrcMode eSrcMode, AL_ECodec eCodec)
{
    (void)eCodec;

    AL_TPicFormat const tPicFormat = AL_EncGetSrcPicFormat(eCMode, uBitDepth, eSrcMode);

    SrcBufDesc srcBufDesc = { AL_GetFourCC(tPicFormat), {} };

    int32_t iPitchY = ComputeYPitch(tDimension.iWidth, tPicFormat);

    int32_t iAlignValue = 8;

    int32_t iStrideHeight = g_StrideHeight != -1 ?
        g_StrideHeight : AL_RoundUp(tDimension.iHeight, iAlignValue);

    SrcBufChunk srcChunk {};

    AL_EPlaneId usedPlanes[AL_MAX_BUFFER_PLANES];
    int32_t iNbPlanes = AL_Plane_GetBufferPixelPlanes(tPicFormat, usedPlanes);

    for (int32_t iPlane = 0; iPlane < iNbPlanes; iPlane++)
    {
        int32_t iPitch = usedPlanes[iPlane] == AL_PLANE_Y ?
            iPitchY : AL_GetChromaPitch(srcBufDesc.tFourCC, iPitchY);
        srcChunk.vPlaneDesc.push_back(
            AL_TPlaneDescription { usedPlanes[iPlane], srcChunk.iChunkSize, iPitch });
        srcChunk.iChunkSize +=
            AL_GetAllocSizeSrc_PixPlane(&tPicFormat, iPitchY, iStrideHeight, usedPlanes[iPlane]);

        if (g_MultiChunk)
        {
            srcBufDesc.vChunks.push_back(srcChunk);
            srcChunk = {};
        }
    }

    if (!g_MultiChunk)
        srcBufDesc.vChunks.push_back(srcChunk);

    return srcBufDesc;
}

/*****************************************************************************/
uint8_t GetNumBufForGop(AL_TEncSettings Settings)
{
    int32_t uNumFields = 1;

    if (AL_IS_INTERLACED(Settings.tChParam[0].eVideoMode))
        uNumFields = 2;
    int32_t uAdditionalBuf = 0;
    return uNumFields * Settings.tChParam[0].tGopParam.uNumB + uAdditionalBuf;
}

/*****************************************************************************/
bool InitStreamBufPool(BufPool& pool, AL_TEncSettings& Settings, int32_t iLayerID,
    uint8_t uNumCore, int32_t iForcedStreamBufferSize, AL_TAllocator* pAllocator)
{
    (void)uNumCore;

    int32_t numStreams;

    AL_TDimension dim =
        { Settings.tChParam[iLayerID].uEncWidth, Settings.tChParam[iLayerID].uEncHeight };
    uint64_t streamSize = iForcedStreamBufferSize;

    if (streamSize == 0)
    {
        streamSize = AL_GetMitigatedMaxNalSize(dim,
            AL_GET_CHROMA_MODE(Settings.tChParam[0].ePicFormat),
            AL_GET_BITDEPTH(Settings.tChParam[0].ePicFormat));

        bool bIsXAVCIntraCBG = AL_IS_XAVC_CBG(Settings.tChParam[0].eProfile)
            && AL_IS_INTRA_PROFILE(Settings.tChParam[0].eProfile);

        if (bIsXAVCIntraCBG)
            streamSize = AL_GetMaxNalSize(dim, AL_GET_CHROMA_MODE(Settings.tChParam[0].ePicFormat),
                AL_GET_BITDEPTH(Settings.tChParam[0].ePicFormat), Settings.tChParam[0].eProfile,
                Settings.tChParam[0].uLevel);
    }

    static const int32_t smoothingStream = 2;
    numStreams = g_defaultMinBuffers + smoothingStream + GetNumBufForGop(Settings);

    if (Settings.tChParam[0].bSubframeLatency)
    {
        numStreams *= Settings.tChParam[0].uNumSlices;

        {
            // Due to rounding, the slices don't have all the same height.
            // Compute size of the biggest slice
            uint64_t lcuSize = 1LL << Settings.tChParam[0].uLog2MaxCuSize;
            uint64_t rndHeight = AL_RoundUp(dim.iHeight, lcuSize);
            streamSize = streamSize * lcuSize *
                (1 + rndHeight / (Settings.tChParam[0].uNumSlices * lcuSize)) / rndHeight;

            /* we need space for the headers on each slice */
            streamSize += AL_ENC_MAX_HEADER_SIZE;
            /* stream size is required to be 32bytes aligned */
            streamSize = AL_RoundUp(streamSize, HW_IP_BURST_ALIGNMENT);
        }
    }

    if (streamSize > INT32_MAX)
        throw std::runtime_error("streamSize(" + std::to_string(streamSize) +
            ") must be lower or equal than INT32_MAX(" + std::to_string(INT32_MAX) + ")");

    auto pMetaData = (AL_TMetaData*)AL_StreamMetaData_Create(AL_MAX_SECTION);
    bool bSucceed = pool.Init(pAllocator, numStreams, streamSize, pMetaData, "stream");
    AL_MetaData_Destroy(pMetaData);

    return bSucceed;
}

/*****************************************************************************/
void InitSrcBufPool(PixMapBufPool& SrcBufPool, AL_TAllocator* pAllocator,
    TFrameInfo& FrameInfo, AL_ESrcMode eSrcMode, int32_t frameBuffersCount, AL_ECodec eCodec)
{
    auto srcBufDesc = GetSrcBufDescription(FrameInfo.tDimension, FrameInfo.iBitDepth,
                                           FrameInfo.eCMode, eSrcMode, eCodec);

    SrcBufPool.SetFormat(FrameInfo.tDimension, srcBufDesc.tFourCC);

    for (auto& vChunk : srcBufDesc.vChunks)
        SrcBufPool.AddChunk(vChunk.iChunkSize, vChunk.vPlaneDesc);

    bool const ret = SrcBufPool.Init(pAllocator, frameBuffersCount, "input");

    if (!ret)
        throw std::runtime_error("src buf pool must succeed init");
}

/*****************************************************************************/
void LayerResources::Init(Config& cfg, AL_TEncoderInfo tEncInfo, int32_t iLayerID,
                          AL_TAllocator* pAllocator, int32_t chanId)
{
    AL_TEncSettings& Settings = cfg.Settings;
    auto const eSrcMode = Settings.tChParam[iLayerID].eSrcMode;

    (void)chanId;
    this->iLayerID = iLayerID;

    {
        layerInputs.push_back(cfg.MainInput);
        layerInputs.insert(layerInputs.end(), cfg.DynamicInputs.begin(), cfg.DynamicInputs.end());
    }

    // --------------------------------------------------------------------------------
    // Stream Buffers
    // --------------------------------------------------------------------------------
    if (!InitStreamBufPool(StreamBufPool, Settings, iLayerID, tEncInfo.uNumCore,
                          cfg.iForceStreamBufSize, pAllocator))
        throw std::runtime_error("Error creating stream buffer pool");

    AL_TDimension tDim =
        { Settings.tChParam[iLayerID].uEncWidth, Settings.tChParam[iLayerID].uEncHeight };

    bool bUsePictureMeta = false;
    bUsePictureMeta |= cfg.RunInfo.printPictureType;

    if (iLayerID == 0 && bUsePictureMeta)
    {
        auto pMeta = (AL_TMetaData*)AL_PictureMetaData_Create();

        if (pMeta == nullptr)
            throw std::runtime_error("Meta must be created");
        bool const bRet = StreamBufPool.AddMetaData(pMeta);

        if (!bRet)
            throw std::runtime_error("Meta must be added in stream pool");
        AL_MetaData_Destroy(pMeta);
    }

    if (cfg.RunInfo.rateCtrlStat != AL_RATECTRL_STAT_MODE_NONE)
    {
        auto pMeta = (AL_TMetaData*)AL_RateCtrlMetaData_CustomCreate(pAllocator,
            cfg.RunInfo.rateCtrlStat, tDim, Settings.tChParam[iLayerID].uLog2MaxCuSize,
            AL_GET_CODEC(Settings.tChParam[iLayerID].eProfile));

        if (pMeta == nullptr)
            throw std::runtime_error("Meta must be created");
        bool const bRet = StreamBufPool.AddMetaData(pMeta);

        if (!bRet)
            throw std::runtime_error("Meta must be added in stream pool");

        AL_MetaData_Destroy(pMeta);
    }

    // --------------------------------------------------------------------------------
    // Application Input/Output Format conversion
    // --------------------------------------------------------------------------------
    const AL_TPicFormat tSrcPicFmt = GetSrcPicFormat(Settings.tChParam[iLayerID]);
    SrcConverterParams tSrcConverterParams =
    {
        { AL_GetSrcWidth(Settings.tChParam[iLayerID]), AL_GetSrcHeight(Settings.tChParam[iLayerID]) },
        layerInputs[iInputIdx].FileInfo.FourCC,
        tSrcPicFmt,
        cfg.eSrcFormat,
    };

    if (IsConversionNeeded(tSrcConverterParams))
        pSrcConv = AllocateSrcConverter(tSrcConverterParams, SrcYuv);

    TFrameInfo tSrcFrameInfo = { tSrcConverterParams.tDim, tSrcConverterParams.tSrcPicFmt.uBitDepth,
                                 tSrcConverterParams.tSrcPicFmt.eChromaMode };

    // --------------------------------------------------------------------------------
    // Source Buffers
    // --------------------------------------------------------------------------------
    int32_t srcBuffersCount = g_defaultMinBuffers + GetNumBufForGop(Settings);;

    InitSrcBufPool(SrcBufPool, pAllocator, tSrcFrameInfo, eSrcMode, srcBuffersCount,
                   static_cast<AL_ECodec>(AL_GET_CODEC(Settings.tChParam[0].eProfile)));

    iPictCount = 0;
    iReadCount = 0;
}

void LayerResources::PushResources(Config& cfg, EncoderSink* enc)
{
    (void)cfg;

    if (frameWriter)
        enc->RecOutput[iLayerID] = std::move(frameWriter);

    for (int32_t i = 0; i < (int)StreamBufPool.GetNumBuf(); ++i)
    {
        std::shared_ptr<AL_TBuffer> pStream =
            StreamBufPool.GetSharedBuffer(AL_EBufMode::AL_BUF_MODE_NONBLOCK);

        if (pStream == nullptr)
            throw std::runtime_error("pStream must exist");

        AL_HEncoder hEnc = enc->hEnc;

        bool bRet = true;

        if (iLayerID == 0)
        {
            int32_t iStreamNum = 1;

            // the look ahead needs one more stream buffer to work AVC due to (potential) multi-core
            if (AL_IS_AVC(cfg.Settings.tChParam[0].eProfile))
                iStreamNum += 1;

            bRet = AL_Encoder_PutStreamBuffer(hEnc, pStream.get());
        }

        if (!bRet)
            throw std::runtime_error("bRet must be true");
    }
}

[[maybe_unused]] void LayerResources::OpenEncoderInput(Config& cfg, AL_HEncoder hEnc)
{
    ChangeInput(cfg, iInputIdx, hEnc);
}

[[maybe_unused]] bool LayerResources::SendInput(Config& cfg, EncoderSink* firstSink,
                                                void* pTraceHooker)
{
    (void)pTraceHooker;
    firstSink->PreprocessFrame();

    return sendInputFileTo(frameReader, SrcBufPool, SrcYuv.get(), cfg,
        layerInputs[iInputIdx].FileInfo, pSrcConv.get(), firstSink, iPictCount, iReadCount);
}

bool LayerResources::sendInputFileTo(std::unique_ptr<FrameReader>& frameReader,
    PixMapBufPool& SrcBufPool, AL_TBuffer* Yuv, Config const& cfg, AL_TYUVFileInfo& FileInfo,
    IConvSrc* pSrcConv, EncoderSink* pEncoderSink, int& iPictCount, int& iReadCount)
{
    if (AL_IS_ERROR_CODE(pEncoderSink->GetLastError()))
    {
        pEncoderSink->ProcessFrame(nullptr);
        return false;
    }

    std::shared_ptr<AL_TBuffer> frame = GetSrcFrame(iReadCount, iPictCount, frameReader, FileInfo,
            SrcBufPool, Yuv, cfg.Settings.tChParam[0], cfg, pSrcConv);

    pEncoderSink->ProcessFrame(frame.get());

    if (!frame)
        return false;

    iPictCount++;
    return true;
}

std::unique_ptr<FrameReader> LayerResources::InitializeFrameReader(Config& cfg,
        std::ifstream& YuvFile, std::string sYuvFileName, std::ifstream& MapFile,
        std::string sMapFileName, AL_TYUVFileInfo& FileInfo)
{
    (void)(MapFile);

    std::unique_ptr<FrameReader> pFrameReader;
    bool bUseCompressedFormat = AL_IsCompressed(FileInfo.FourCC);
    bool bHasCompressionMapFile = !sMapFileName.empty();

    if (bUseCompressedFormat != bHasCompressionMapFile)
        throw std::runtime_error(std::string("Providing a map file is ") +
                std::string(bUseCompressedFormat ? "mandatory" : "forbidden") + " when using " +
                std::string(bUseCompressedFormat ? "compressed" : "uncompressed") + " input.");

    YuvFile.close();
    OpenInput(YuvFile, sYuvFileName);

    if (!bUseCompressedFormat)
        pFrameReader = std::unique_ptr<FrameReader>(
                new UnCompFrameReader(YuvFile, FileInfo, cfg.RunInfo.bLoop));

    pFrameReader->SeekAbsolute(cfg.RunInfo.iFirstPict + iReadCount);

    return pFrameReader;
}

void LayerResources::ChangeInput(Config& cfg, int32_t iInputIdx, AL_HEncoder hEnc)
{
    (void)hEnc;

    if (iInputIdx < static_cast<int>(layerInputs.size()))
    {
        this->iInputIdx = iInputIdx;
        AL_TDimension inputDim = { layerInputs[iInputIdx].FileInfo.PictWidth,
                                   layerInputs[iInputIdx].FileInfo.PictHeight };
        bool bResChange = (inputDim.iWidth != AL_GetSrcWidth(cfg.Settings.tChParam[iLayerID]))
            || (inputDim.iHeight != AL_GetSrcHeight(cfg.Settings.tChParam[iLayerID]));

    if (bResChange)
    {
        /* No resize with dynamic resolution changes */
        cfg.Settings.tChParam[iLayerID].uEncWidth =
                cfg.Settings.tChParam[iLayerID].uSrcWidth = inputDim.iWidth;
        cfg.Settings.tChParam[iLayerID].uEncHeight =
                cfg.Settings.tChParam[iLayerID].uSrcHeight = inputDim.iHeight;

        AL_Encoder_SetInputResolution(hEnc, inputDim);
    }

    frameReader = InitializeFrameReader(cfg, YuvFile,
                                        layerInputs[iInputIdx].YUVFileName,
                                        MapFile,
                                        cfg.MainInput.sMapFileName,
                                        layerInputs[iInputIdx].FileInfo);
    }
}

} // anonymous namespace


class EncoderContext : public EncContext
{
public:
    EncoderContext(Ptr<Config> cfg, Ptr<Device>& device, DataCallback dataCallback);
    virtual ~EncoderContext();

    virtual void writeFrame(Ptr<Frame> frame) override;
    virtual std::shared_ptr<AL_TBuffer> getSharedBuffer() override;
    virtual bool waitForCompletion() override;
    virtual void notifyGMV(int32_t frameIndex, int32_t gmVectorX, int32_t gmVectorY) override;
    virtual String statistics() const override;
    virtual AL_HEncoder hEnc() override { return enc_->hEnc; }

private:
    class EncLibInitter
    {
        EncLibInitter() {}

        void init()
        {

            AL_ELibEncoderArch eArch = AL_LIB_ENCODER_ARCH_HOST;
#ifdef HAVE_VCU2_CTRLSW
            eArch = AL_LIB_ENCODER_ARCH_RISCV;
#endif
            if (!AL_IS_SUCCESS_CODE(AL_Lib_Encoder_Init(eArch)))
                throw std::runtime_error("Can't setup encode library");
        }

    public:
        ~EncLibInitter()
        {
            AL_Lib_Encoder_DeInit();
        }

        static std::shared_ptr<EncLibInitter> getInstance()
        {
            static std::weak_ptr<EncLibInitter> instance;
            static std::mutex mutex;

            std::lock_guard<std::mutex> lock(mutex);
            auto ptr = instance.lock();
            if (!ptr)
            {
                ptr = std::shared_ptr<EncLibInitter>(new EncLibInitter());
                instance = ptr;
            }
            ptr->init(); // init is called each time, even if instance already exists
                         // AL_Lib_Encoder_DeInit is called only when the last instance is destroyed
            return ptr;
        }
    };

    std::unique_ptr<EncoderSink> channelMain(Config& cfg,
        std::vector<std::unique_ptr<LayerResources>>& pLayerResources,
        Ptr<Device> device, int32_t chanId, DataCallback dataCallback);

    std::shared_ptr<EncLibInitter> libInit_;
    std::unique_ptr<EncoderSink> enc_;
    std::vector<std::unique_ptr<LayerResources>> layerResources_;
};


EncoderContext::EncoderContext(Ptr<Config> cfg, Ptr<Device>& device, DataCallback dataCallback)
{
    layerResources_.emplace_back(std::make_unique<LayerResources>());

    InitializePlateform();

    {
        auto& Settings = cfg->Settings;
        auto& RecFileName = cfg->RecFileName;

        // Note: AL_Settings_SetDefaultParam, SetMoreDefaults, and eSrcMode settings
        // are now called in vcuenc.cpp VCUEncoder constructor before creating EncoderContext

        if (!RecFileName.empty())
        {
            Settings.tChParam[0].eEncOptions = (AL_EChEncOption)(Settings.tChParam[0].eEncOptions
                                               | AL_OPT_FORCE_REC);
        }

        ValidateConfig(*cfg);
    }

    libInit_ = EncLibInitter::getInstance();

    device = Device::create(Device::ENCODER);
    enc_ = channelMain(*cfg, layerResources_, device, 0, dataCallback);
}

EncoderContext::~EncoderContext()
{
    enc_.reset();
    layerResources_[0].reset();
}

void EncoderContext::writeFrame(Ptr<Frame> frame)
{
    if (frame)
        enc_->ProcessFrame(frame->getBuffer());
    else
        enc_->ProcessFrame(nullptr);
}


std::shared_ptr<AL_TBuffer> EncoderContext::getSharedBuffer()
{
    return layerResources_[0]->SrcBufPool.GetSharedBuffer();
}

bool EncoderContext::waitForCompletion()
{
    return enc_->waitForCompletion();
}

void EncoderContext::notifyGMV(int32_t frameIndex, int32_t gmVectorX, int32_t gmVectorY)
{
#ifdef HAVE_VCU2_CTRLSW
    AL_Encoder_NotifyGMV(enc_->hEnc, frameIndex, gmVectorX, gmVectorY);
#else
    (void)frameIndex;
    (void)gmVectorX;
    (void)gmVectorY;
#endif
}

String EncoderContext::statistics() const
{
    String stats;
    if (enc_) {
        stats += std::to_string(enc_->nrFrames()) + " pictures encoded\n";
        stats += "Average FrameRate = " + std::to_string(enc_->fps()) + " Fps\n";
    }
    return stats;
}

std::unique_ptr<EncoderSink> EncoderContext::channelMain(Config& cfg,
        std::vector<std::unique_ptr<LayerResources>>& pLayerResources,
        Ptr<Device> device, int32_t chanId, DataCallback dataCallback)
{
    auto& Settings = cfg.Settings;

    /* null if not supported */
    //void* pTraceHook {};
    std::unique_ptr<EncoderSink> enc;

    auto pAllocator = device->getAllocator();
    auto pScheduler = (AL_IEncScheduler*)device->getScheduler();

#ifdef HAVE_VCU2_CTRLSW
    auto ctx = device->getCtx();
#endif

    AL_EVENT hFinished = Rtos_CreateEvent(false);
    RCPlugin_Init(&cfg.Settings, &cfg.Settings.tChParam[0], pAllocator);

    auto OnScopeExit = std::unique_ptr<void, std::function<void(void*)>>(
        reinterpret_cast<void*>(1),
        [&](void*) {
            Rtos_DeleteEvent(hFinished);
            AL_Allocator_Free(pAllocator, cfg.Settings.hRcPluginDmaContext);
        });


    // --------------------------------------------------------------------------------
    // Create Encoder

#ifdef HAVE_VCU2_CTRLSW
    (void) pScheduler;
    enc.reset(new EncoderSink(cfg, ctx, pAllocator));
#else
    enc.reset(new EncoderSink(cfg, pScheduler, pAllocator));
#endif

    EncoderSink* pFirstEncoderSink = enc.get();

    // --------------------------------------------------------------------------------
    // Allocate/Push Layers resources
    AL_TEncoderInfo tEncInfo;
    AL_Encoder_GetInfo(enc->hEnc, &tEncInfo);

    for (size_t i = 0; i < pLayerResources.size(); i++)
    {
        auto multisinkRec = std::unique_ptr<MultiSink>(new MultiSink);
        pLayerResources[i]->Init(cfg, tEncInfo, i, pAllocator, chanId);
        pLayerResources[i]->PushResources(cfg, enc.get());

        // Rec file creation
        std::string LayerRecFileName = cfg.RecFileName;

        if (!LayerRecFileName.empty())
        {

#ifdef HAVE_VCU2_CTRLSW
            if (Settings.tChParam[0].eEncOptions & AL_OPT_COMPRESS)
            {
                std::unique_ptr<IFrameSink> recOutput(createCompFrameSink(LayerRecFileName,
                        LayerRecFileName + ".map", Settings.tChParam[0].eRecStorageMode, 0));
                multisinkRec->addSink(recOutput);
            }
            else
            {
                std::unique_ptr<IFrameSink> recOutput(createUnCompFrameSink(LayerRecFileName,
                                                                            AL_FB_RASTER));
                multisinkRec->addSink(recOutput);
            }
#elif defined(HAVE_VCU_CTRLSW)
            std::unique_ptr<IFrameSink> recOutput(
                    createUnCompFrameSink(LayerRecFileName, AL_FB_RASTER));
            multisinkRec->addSink(recOutput);
#endif
        }
        enc->RecOutput[i] = std::move(multisinkRec);
    }

    enc->dataCallback_ = dataCallback;

    // --------------------------------------------------------------------------------
    // Set Callbacks
    pFirstEncoderSink->SetChangeSourceCallback([&](int32_t iInputIdx, int32_t iLayerID)
                           {
                                pLayerResources[iLayerID]->ChangeInput(cfg, iInputIdx, enc->hEnc);
                           });

    OnScopeExit.release();
    return enc;
}


/*static*/
Ptr<EncContext> EncContext::create(Ptr<Config> cfg, Ptr<Device>& device, DataCallback dataCallback)
{
    Ptr<EncoderContext> ctx(new EncoderContext(cfg, device, dataCallback));
    return ctx;
}

template<>
String toString<AL_TPicFormat>(AL_TPicFormat const& format)
{
    String str = "chroma=";
    str += AL_ChromaModeToString(format.eChromaMode);
    str += ", alpha=";
    str += AL_AlphaModeToString(format.eAlphaMode);
    str += ", bitDepth=";
    str += std::to_string(static_cast<int>(format.uBitDepth));
    str += ", storage=";
    str += AL_FbStorageModeToString(format.eStorageMode);
    str += ", plane=";
    str += AL_PlaneModeToString(format.ePlaneMode);
    str += ", componentOrder=";
    str += AL_ComponentOrderToString(format.eComponentOrder);
    str += ", samplePack=";
    str += AL_SamplePackModeToString(format.eSamplePackMode);
    str += ", compressed=";
    str += AL_CompressedToString(format.bCompressed);
    str += ", msb=";
    str += AL_MsbToString(format.bMSB);
    return str;
}

} // namespace vcucodec
} // namespace cv

#endif // defined(HAVE_VCU_CTRLSW) || defined(HAVE_VCU2_CTRLSW)
