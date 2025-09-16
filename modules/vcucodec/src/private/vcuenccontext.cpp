// Modifications Copyright(C) [2025] Advanced Micro Devices, Inc. All rights reserved)

// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT
#if defined(HAVE_VCU_CTRLSW) || defined(HAVE_VCU2_CTRLSW)

//#include "ctrlsw_enc.hpp"
#include <dirent.h>


#include "resource.h"

#include "sink_encoder.h"
#include "sink_lookahead.h"

#include "lib_app/AL_RasterConvert.hpp"
#include "lib_app/BuildInfo.hpp"
#include "lib_app/FileUtils.hpp"
#include "lib_app/PixMapBufPool.hpp"

#ifdef HAVE_VCU2_CTRLSW
#include "lib_app/CompFrameReader.hpp"
#include "lib_app/CompFrameCommon.hpp"
#endif
#include "lib_app/UnCompFrameReader.hpp"
#include "lib_app/SinkFrame.hpp"
#include "lib_app/plateform.hpp"

extern "C" {
#include "lib_common/Round.h"
#include "lib_common/StreamBuffer.h"
#include "lib_common_enc/IpEncFourCC.h"
#include "lib_common_enc/RateCtrlMeta.h"
#include "lib_encode/lib_encoder.h"
}

#include "vcudevice.hpp"
#include "vcuenccontext.hpp"
#include "TwoPassMngr.h"

#if !defined(HAS_COMPIL_FLAGS)
#define AL_COMPIL_FLAGS ""
#endif

#include <regex>

namespace cv{
namespace vcucodec{
class Device;

namespace { // anonymous

using Config = cv::vcucodec::EncContext::Config;
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

    void PushResources(Config& cfg, EncoderSink* enc , EncoderLookAheadSink* encFirstPassLA);

    void OpenEncoderInput(Config& cfg, AL_HEncoder hEnc);

    bool SendInput(Config& cfg, IEncoderSink* firstSink, void* pTraceHook);

    bool sendInputFileTo(std::unique_ptr<FrameReader>& frameReader, PixMapBufPool& SrcBufPool,
                         AL_TBuffer* Yuv, Config const& cfg, AL_TYUVFileInfo& FileInfo,
                         IConvSrc* pSrcConv, IEncoderSink* pEncoderSink, int& iPictCount,
                         int& iReadCount);

    std::unique_ptr<FrameReader> InitializeFrameReader(Config& cfg, std::ifstream& YuvFile,
         std::string sYuvFileName, std::ifstream& MapFile, std::string sMapFileName,
         AL_TYUVFileInfo& FileInfo);

    void ChangeInput(Config& cfg, int32_t iInputIdx, AL_HEncoder hEnc);

    BufPool StreamBufPool;
    BufPool QpBufPool;
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

static int32_t g_StrideHeight = -1;
static int32_t g_Stride = -1;
static int32_t constexpr g_defaultMinBuffers = 2;
static bool g_MultiChunk = false;

void DisplayVersionInfo(void)
{
    ::DisplayVersionInfo(AL_ENCODER_COMPANY,
                         AL_ENCODER_PRODUCT_NAME,
                         AL_ENCODER_VERSION,
                         AL_ENCODER_COPYRIGHT,
                         AL_ENCODER_COMMENTS);
}


/*****************************************************************************/
bool checkQPTableFolder(Config& cfg)
{
    std::regex qp_file_per_frame_regex("QP(^|)(s|_[0-9]+)\\.hex");

    if (!FolderExists(cfg.MainInput.sQPTablesFolder))
        return false;

    return FileExists(cfg.MainInput.sQPTablesFolder, qp_file_per_frame_regex);
}

void ValidateConfig(Config& cfg)
{
    std::string const invalid_settings("Invalid settings, check the [SETTINGS] section of your "
            "configuration file or check your commandline (use -h to get help)");

    if (cfg.MainInput.YUVFileName.empty())
        throw std::runtime_error("No YUV input was given, specify it in the [INPUT] section of your"
                                 " configuration file or in your commandline (use -h to get help)");

    if (!cfg.MainInput.sQPTablesFolder.empty()
            && ((cfg.RunInfo.eGenerateQpMode & AL_GENERATE_QP_TABLE_MASK) != AL_GENERATE_LOAD_QP))
        throw std::runtime_error("QPTablesFolder can only be specified with Load QP control mode");

    SetConsoleColor(CC_RED);

    FILE* out = stdout;

    if (!g_Verbosity)
        out = nullptr;

    auto const MaxLayer = cfg.Settings.NumLayer - 1;

    for(int32_t i = 0; i < cfg.Settings.NumLayer; ++i)
    {
        auto const err = AL_Settings_CheckValidity(&cfg.Settings, &cfg.Settings.tChParam[i], out);

        if (err != 0)
        {
            std::stringstream ss;
            ss << "Found: " << err << " errors(s). " << invalid_settings;
            throw std::runtime_error(ss.str());
        }

        if ((cfg.RunInfo.eGenerateQpMode & AL_GENERATE_QP_TABLE_MASK) == AL_GENERATE_LOAD_QP)
        {
            if (!checkQPTableFolder(cfg))
                throw std::runtime_error("No QP File found");
        }

        auto const incoherencies = AL_Settings_CheckCoherency(&cfg.Settings,
                &cfg.Settings.tChParam[i], cfg.MainInput.FileInfo.FourCC, out);

        if (incoherencies < 0)
            throw std::runtime_error("Fatal coherency error in settings (layer[" +
                                     std::to_string(i) + "/" + std::to_string(MaxLayer) + "])");
    }

    if (cfg.Settings.TwoPass == 1)
        AL_TwoPassMngr_SetPass1Settings(cfg.Settings);

    SetConsoleColor(CC_DEFAULT);
}

void SetMoreDefaults(Config& cfg)
{
    auto& FileInfo = cfg.MainInput.FileInfo;
    auto& Settings = cfg.Settings;
    auto& RecFourCC = cfg.RecFourCC;
    auto& RunInfo = cfg.RunInfo;

    if (RunInfo.encDevicePaths.empty())
        RunInfo.encDevicePaths = ENCODER_DEVICES;

    if (FileInfo.FrameRate == 0)
        FileInfo.FrameRate = Settings.tChParam[0].tRCParam.uFrameRate;

    if (RecFourCC == FOURCC(NULL))
    {
        AL_TPicFormat tOutPicFormat;

        if (AL_GetPicFormat(FileInfo.FourCC, &tOutPicFormat))
        {
            if (tOutPicFormat.eComponentOrder != AL_COMPONENT_ORDER_RGB &&
                    tOutPicFormat.eComponentOrder != AL_COMPONENT_ORDER_BGR)
                tOutPicFormat.eChromaMode = AL_GET_CHROMA_MODE(Settings.tChParam[0].ePicFormat);

            tOutPicFormat.ePlaneMode = tOutPicFormat.eChromaMode == AL_CHROMA_MONO ?
                    AL_PLANE_MODE_MONOPLANE : tOutPicFormat.ePlaneMode;
            tOutPicFormat.uBitDepth = AL_GET_BITDEPTH(Settings.tChParam[0].ePicFormat);
            RecFourCC = AL_GetFourCC(tOutPicFormat);
        }
        else
        {
            RecFourCC = FileInfo.FourCC;
        }
    }

    AL_TPicFormat tRecPicFormat;

    if (AL_GetPicFormat(RecFourCC, &tRecPicFormat))
    {
        auto& RecFileName = cfg.RecFileName;

        if (!RecFileName.empty())
            if (tRecPicFormat.eStorageMode != AL_FB_RASTER && !tRecPicFormat.bCompressed)
                throw std::runtime_error(
                        "Reconstructed storage format can only be tiled if compressed.");
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
bool InitQpBufPool(BufPool& pool, AL_TEncSettings& Settings, AL_TEncChanParam& tChParam,
                          int32_t frameBuffersCount, AL_TAllocator* pAllocator)
{
    (void)Settings;

    if (!AL_IS_QP_TABLE_REQUIRED(Settings.eQpTableMode))
        return true;

    AL_TDimension tDim = { tChParam.uEncWidth, tChParam.uEncHeight };
    return pool.Init(pAllocator, frameBuffersCount,
        AL_GetAllocSizeEP2(tDim, static_cast<AL_ECodec>(AL_GET_CODEC(tChParam.eProfile)),
        tChParam.uLog2MaxCuSize), nullptr, "qp-ext");
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

    for(int32_t iPlane = 0; iPlane < iNbPlanes; iPlane++)
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

    {
        static const int32_t smoothingStream = 2;
        numStreams = g_defaultMinBuffers + smoothingStream + GetNumBufForGop(Settings);
    }

    bool bHasLookAhead;
    bHasLookAhead = AL_TwoPassMngr_HasLookAhead(Settings);

    if (bHasLookAhead)
    {
        int32_t extraLookAheadStream = 1;

        // the look ahead needs one more stream buffer to work in AVC due to (potential) multi-core
        if (AL_IS_AVC(Settings.tChParam[0].eProfile))
            extraLookAheadStream += 1;
        numStreams += extraLookAheadStream;
    }

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

    for(auto& vChunk : srcBufDesc.vChunks)
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

    bUsePictureMeta |= AL_TwoPassMngr_HasLookAhead(Settings);

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
    // Tuning Input Buffers
    // --------------------------------------------------------------------------------
    int32_t frameBuffersCount = g_defaultMinBuffers + GetNumBufForGop(Settings);

    {
        frameBuffersCount = g_defaultMinBuffers + GetNumBufForGop(Settings);

        if (AL_TwoPassMngr_HasLookAhead(Settings))
        {
            frameBuffersCount += Settings.LookAhead + (GetNumBufForGop(Settings) * 2);

            if (AL_IS_AVC(cfg.Settings.tChParam[0].eProfile))
                frameBuffersCount += 1;
        }

    }

    if (!InitQpBufPool(QpBufPool, Settings, Settings.tChParam[iLayerID], frameBuffersCount, pAllocator))
        throw std::runtime_error("Error creating QP buffer pool");

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
    int32_t srcBuffersCount = frameBuffersCount;

    InitSrcBufPool(SrcBufPool, pAllocator, tSrcFrameInfo, eSrcMode, srcBuffersCount,
                   static_cast<AL_ECodec>(AL_GET_CODEC(Settings.tChParam[0].eProfile)));

    iPictCount = 0;
    iReadCount = 0;
}

void LayerResources::PushResources(Config& cfg, EncoderSink* enc ,
                                   EncoderLookAheadSink* encFirstPassLA)
{
    (void)cfg;
    QPBuffers::QPLayerInfo qpInf
    {
        &QpBufPool,
        layerInputs[iInputIdx].sQPTablesFolder,
        layerInputs[iInputIdx].sRoiFileName
    };

    enc->AddQpBufPool(qpInf, iLayerID);

    if (AL_TwoPassMngr_HasLookAhead(cfg.Settings))
    {
        encFirstPassLA->AddQpBufPool(qpInf, iLayerID);
    }

    if (frameWriter)
        enc->RecOutput[iLayerID] = std::move(frameWriter);

    for(int32_t i = 0; i < (int)StreamBufPool.GetNumBuf(); ++i)
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

            if (AL_TwoPassMngr_HasLookAhead(cfg.Settings) && i < iStreamNum)
                hEnc = encFirstPassLA->hEnc;

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

[[maybe_unused]] bool LayerResources::SendInput(Config& cfg, IEncoderSink* firstSink,
                                                void* pTraceHooker)
{
    (void)pTraceHooker;
    firstSink->PreprocessFrame();

    return sendInputFileTo(frameReader, SrcBufPool, SrcYuv.get(), cfg,
        layerInputs[iInputIdx].FileInfo, pSrcConv.get(), firstSink, iPictCount, iReadCount);
}

bool LayerResources::sendInputFileTo(std::unique_ptr<FrameReader>& frameReader,
    PixMapBufPool& SrcBufPool, AL_TBuffer* Yuv, Config const& cfg, AL_TYUVFileInfo& FileInfo,
    IConvSrc* pSrcConv, IEncoderSink* pEncoderSink, int& iPictCount, int& iReadCount)
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

std::unique_ptr<EncoderSink> ChannelMain(Config& cfg,
        std::vector<std::unique_ptr<LayerResources>>& pLayerResources,
        Ptr<Device> device, int32_t chanId, DataCallback dataCallback)
{
    auto& Settings = cfg.Settings;

    /* null if not supported */
    //void* pTraceHook {};
    std::unique_ptr<EncoderSink> enc;
    std::unique_ptr<EncoderLookAheadSink> encFirstPassLA;

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
    enc.reset(new EncoderSink(cfg, ctx, pAllocator));
#else
    enc.reset(new EncoderSink(cfg, pScheduler, pAllocator));
#endif

    IEncoderSink* pFirstEncoderSink = enc.get();

    if (AL_TwoPassMngr_HasLookAhead(cfg.Settings))
    {

#ifdef HAVE_VCU2_CTRLSW
        if (ctx)
            encFirstPassLA.reset(new EncoderLookAheadSink(pFirstEncoderSink, cfg, ctx, pAllocator));
        else
#endif
            encFirstPassLA.reset(new EncoderLookAheadSink(pFirstEncoderSink, cfg, pScheduler, pAllocator));

        pFirstEncoderSink = encFirstPassLA.get();
    }

    // --------------------------------------------------------------------------------
    // Allocate/Push Layers resources
    AL_TEncoderInfo tEncInfo;
    AL_Encoder_GetInfo(enc->hEnc, &tEncInfo);

    for(size_t i = 0; i < pLayerResources.size(); i++)
    {
        auto multisinkRec = std::unique_ptr<MultiSink>(new MultiSink);
        pLayerResources[i]->Init(cfg, tEncInfo, i, pAllocator, chanId);
        pLayerResources[i]->PushResources(cfg, enc.get(), encFirstPassLA.get());

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

#if 0 // encoder input file is not needed for opencv case
    for(int32_t i = 0; i < Settings.NumLayer; ++i)
        pLayerResources[i]->OpenEncoderInput(cfg, enc->hEnc);
#endif
    OnScopeExit.release();
    return enc;
}

/*****************************************************************************/
std::unique_ptr<EncoderSink> CtrlswEncOpen(Config& cfg,
        std::vector<std::unique_ptr<LayerResources>>& pLayerResources,
        Ptr<Device>& device, DataCallback dataCallback)
{
  std::unique_ptr<EncoderSink> enc;
  InitializePlateform();

    {
        auto& Settings = cfg.Settings;
        auto& RecFileName = cfg.RecFileName;

        AL_Settings_SetDefaultParam(&Settings);
        SetMoreDefaults(cfg);

        if (!RecFileName.empty())
        {
            Settings.tChParam[0].eEncOptions = (AL_EChEncOption)(Settings.tChParam[0].eEncOptions
                                               | AL_OPT_FORCE_REC);
        }

#ifdef HAVE_VCU2_CTRLSW
        Settings.tChParam[0].bUseGMV = !cfg.sGMVFileName.empty();
#endif

        AL_ESrcMode eSrcMode = SrcFormatToSrcMode(cfg.eSrcFormat);

        for(uint8_t uLayer = 0; uLayer < cfg.Settings.NumLayer; uLayer++)
            Settings.tChParam[uLayer].eSrcMode = eSrcMode;

        DisplayVersionInfo();

        ValidateConfig(cfg);
    }

    AL_ELibEncoderArch eArch = AL_LIB_ENCODER_ARCH_HOST;

#ifdef HAVE_VCU2_CTRLSW
    eArch = AL_LIB_ENCODER_ARCH_RISCV;
#endif

    if (!AL_IS_SUCCESS_CODE(AL_Lib_Encoder_Init(eArch)))
        throw std::runtime_error("Can't setup encode library");

    device = Device::create(Device::ENCODER);
    enc = ChannelMain(cfg, pLayerResources, device, 0, dataCallback);

    return enc;
}

} // anonymous namespace

class EncoderContext : public EncContext
{
public:
    EncoderContext(Ptr<Config> cfg, Ptr<Device>& device, DataCallback dataCallback);
    virtual ~EncoderContext();

    virtual void writeFrame(std::shared_ptr<AL_TBuffer> sourceBuffer) override;
    virtual std::shared_ptr<AL_TBuffer> getSharedBuffer() override;
    virtual bool waitForCompletion() override;
    virtual void notifyGMV(int32_t frameIndex, int32_t gmVectorX, int32_t gmVectorY) override;

private:
    std::unique_ptr<EncoderSink> enc_;
    std::vector<std::unique_ptr<LayerResources>> layerResources_;
};


EncoderContext::EncoderContext(Ptr<Config> cfg, Ptr<Device>& device, DataCallback dataCallback)
{
    layerResources_.emplace_back(std::make_unique<LayerResources>());
    enc_ = CtrlswEncOpen(*cfg, layerResources_, device, dataCallback);
}

EncoderContext::~EncoderContext()
{
    enc_.reset();
    layerResources_[0].reset();
    AL_Lib_Encoder_DeInit();
}

void EncoderContext::writeFrame(std::shared_ptr<AL_TBuffer> sourceBuffer)
{
    enc_->ProcessFrame(sourceBuffer.get());
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


/*static*/
Ptr<EncContext> EncContext::create(Ptr<Config> cfg, Ptr<Device>& device, DataCallback dataCallback)
{
    Ptr<EncoderContext> ctx(new EncoderContext(cfg, device, dataCallback));
    return ctx;
}

} // namespace vcucodec
} // namespace cv

#endif // defined(HAVE_VCU_CTRLSW) || defined(HAVE_VCU2_CTRLSW)
