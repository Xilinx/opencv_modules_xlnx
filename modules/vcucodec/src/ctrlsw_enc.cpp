// Modifications Copyright(C) [2025] Advanced Micro Devices, Inc. All rights reserved)

// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT
#if defined(HAVE_VCU_CTRLSW) || defined(HAVE_VCU2_CTRLSW)

#include "ctrlsw_enc.hpp"

static int32_t g_numFrameToRepeat;
static int32_t g_StrideHeight = -1;
static int32_t g_Stride = -1;
static int32_t constexpr g_defaultMinBuffers = 2;
static bool g_MultiChunk = false;

#if 0
/*****************************************************************************/
static AL_HANDLE alignedAlloc(AL_TAllocator* pAllocator, char const* pBufName, uint32_t uSize, uint32_t uAlign, uint32_t* uAllocatedSize, uint32_t* uAlignmentOffset)
{
  *uAllocatedSize = 0;
  *uAlignmentOffset = 0;

  uSize += uAlign;

  auto pBuf = AL_Allocator_AllocNamed(pAllocator, uSize, pBufName);

  if(pBuf == nullptr)
    return nullptr;

  *uAllocatedSize = uSize;
  AL_PADDR pAddr = AL_Allocator_GetPhysicalAddr(pAllocator, pBuf);
  *uAlignmentOffset = AL_PhysAddrRoundUp(pAddr, uAlign) - pAddr;

  return pBuf;
}
#endif

#if 0
/*****************************************************************************/
static void DisplayBuildInfo(void)
{
  BuildInfoDisplay displayBuildInfo {
    SCM_REV_SW, SCM_BRANCH, AL_CONFIGURE_COMMANDLINE, AL_COMPIL_FLAGS, DELIVERY_BUILD_NUMBER, DELIVERY_SCM_REV, DELIVERY_DATE
  };
  displayBuildInfo.displayFeatures = [=](void)
                                     {
                                     };

  displayBuildInfo();
}
#endif

static void DisplayVersionInfo(void)
{
  DisplayVersionInfo(AL_ENCODER_COMPANY,
                     AL_ENCODER_PRODUCT_NAME,
                     AL_ENCODER_VERSION,
                     AL_ENCODER_COPYRIGHT,
                     AL_ENCODER_COMMENTS);
}

/*****************************************************************************/
void SetDefaults(ConfigFile& cfg)
{
  cfg.BitstreamFileName = "Stream.bin";
  cfg.RecFourCC = FOURCC(NULL);
  AL_Settings_SetDefaults(&cfg.Settings);
  cfg.MainInput.FileInfo.FourCC = FOURCC(I420);
  cfg.MainInput.FileInfo.FrameRate = 0;
  cfg.MainInput.FileInfo.PictHeight = 0;
  cfg.MainInput.FileInfo.PictWidth = 0;
  cfg.RunInfo.encDevicePaths = {};
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
  cfg.strict_mode = false;
  cfg.iForceStreamBufSize = 0;
}

#if 0
static void introspect(ConfigFile& cfg)
{
  (void)cfg;
  throw runtime_error("introspection is not compiled in");
}
#endif

void SetCodingResolution(ConfigFile& cfg)
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

/*****************************************************************************/
static bool checkQPTableFolder(ConfigFile& cfg)
{
  std::regex qp_file_per_frame_regex("QP(^|)(s|_[0-9]+)\\.hex");

#ifdef HAVE_VCU2_CTRLSW
  if(!checkFolder(cfg.MainInput.sQPTablesFolder))
#elif defined(HAVE_VCU_CTRLSW)
  if(!FolderExists(cfg.MainInput.sQPTablesFolder))
#endif
    return false;

#ifdef HAVE_VCU2_CTRLSW
  return checkFileAvailability(cfg.MainInput.sQPTablesFolder, qp_file_per_frame_regex);
#elif defined(HAVE_VCU_CTRLSW)
  return FileExists(cfg.MainInput.sQPTablesFolder, qp_file_per_frame_regex);
#endif
}

static void ValidateConfig(ConfigFile& cfg)
{
  string const invalid_settings("Invalid settings, check the [SETTINGS] section of your configuration file or check your commandline (use -h to get help)");

  if(cfg.MainInput.YUVFileName.empty())
    throw runtime_error("No YUV input was given, specify it in the [INPUT] section of your configuration file or in your commandline (use -h to get help)");

  if(!cfg.MainInput.sQPTablesFolder.empty() && ((cfg.RunInfo.eGenerateQpMode & AL_GENERATE_QP_TABLE_MASK) != AL_GENERATE_LOAD_QP))
    throw runtime_error("QPTablesFolder can only be specified with Load QP control mode");

  SetConsoleColor(CC_RED);

  FILE* out = stdout;

  if(!g_Verbosity)
    out = nullptr;

  auto const MaxLayer = cfg.Settings.NumLayer - 1;

  for(int32_t i = 0; i < cfg.Settings.NumLayer; ++i)
  {
    auto const err = AL_Settings_CheckValidity(&cfg.Settings, &cfg.Settings.tChParam[i], out);

    if(err != 0)
    {
      stringstream ss;
      ss << "Found: " << err << " errors(s). " << invalid_settings;
      throw runtime_error(ss.str());
    }

    if((cfg.RunInfo.eGenerateQpMode & AL_GENERATE_QP_TABLE_MASK) == AL_GENERATE_LOAD_QP)
    {
      if(!checkQPTableFolder(cfg))
        throw runtime_error("No QP File found");
    }

    auto const incoherencies = AL_Settings_CheckCoherency(&cfg.Settings, &cfg.Settings.tChParam[i], cfg.MainInput.FileInfo.FourCC, out);

    if(incoherencies < 0)
      throw runtime_error("Fatal coherency error in settings (layer[" + to_string(i) + "/" + to_string(MaxLayer) + "])");

  }

  if(cfg.Settings.TwoPass == 1)
    AL_TwoPassMngr_SetPass1Settings(cfg.Settings);

  SetConsoleColor(CC_DEFAULT);
}

static void SetMoreDefaults(ConfigFile& cfg)
{
  auto& FileInfo = cfg.MainInput.FileInfo;
  auto& Settings = cfg.Settings;
  auto& RecFourCC = cfg.RecFourCC;
  auto& RunInfo = cfg.RunInfo;

  if(RunInfo.encDevicePaths.empty())
    RunInfo.encDevicePaths = ENCODER_DEVICES;

  if(FileInfo.FrameRate == 0)
    FileInfo.FrameRate = Settings.tChParam[0].tRCParam.uFrameRate;

  if(RecFourCC == FOURCC(NULL))
  {
    AL_TPicFormat tOutPicFormat;

    if(AL_GetPicFormat(FileInfo.FourCC, &tOutPicFormat))
    {
      if(tOutPicFormat.eComponentOrder != AL_COMPONENT_ORDER_RGB && tOutPicFormat.eComponentOrder != AL_COMPONENT_ORDER_BGR)
        tOutPicFormat.eChromaMode = AL_GET_CHROMA_MODE(Settings.tChParam[0].ePicFormat);
      tOutPicFormat.ePlaneMode = tOutPicFormat.eChromaMode == AL_CHROMA_MONO ? AL_PLANE_MODE_MONOPLANE : tOutPicFormat.ePlaneMode;
      tOutPicFormat.uBitDepth = AL_GET_BITDEPTH(Settings.tChParam[0].ePicFormat);
      RecFourCC = AL_GetFourCC(tOutPicFormat);
    }
    else
    {
      RecFourCC = FileInfo.FourCC;
    }
  }

  AL_TPicFormat tRecPicFormat;

  if(AL_GetPicFormat(RecFourCC, &tRecPicFormat))
  {
    auto& RecFileName = cfg.RecFileName;

    if(!RecFileName.empty())
      if(tRecPicFormat.eStorageMode != AL_FB_RASTER && !tRecPicFormat.bCompressed)
        throw runtime_error("Reconstructed storage format can only be tiled if compressed.");
  }
}

/*****************************************************************************/
static shared_ptr<AL_TBuffer> AllocateConversionBuffer(int32_t iWidth, int32_t iHeight, TFourCC tFourCC)
{
  AL_TBuffer* pYuv = AllocateDefaultYuvIOBuffer(AL_TDimension { iWidth, iHeight }, tFourCC);

  if(pYuv == nullptr)
    return nullptr;
  return shared_ptr<AL_TBuffer>(pYuv, &AL_Buffer_Destroy);
}

static bool ReadSourceFrameBuffer(AL_TBuffer* pBuffer, AL_TBuffer* conversionBuffer, unique_ptr<FrameReader> const& frameReader, AL_TDimension tUpdatedDim, IConvSrc* hConv)
{

  AL_PixMapBuffer_SetDimension(pBuffer, tUpdatedDim);

  if(hConv)
  {
    AL_PixMapBuffer_SetDimension(conversionBuffer, tUpdatedDim);

    if(!frameReader->ReadFrame(conversionBuffer))
      return false;
    hConv->ConvertSrcBuf(conversionBuffer, pBuffer);
  }
  else
    return frameReader->ReadFrame(pBuffer);

  return true;
}

static shared_ptr<AL_TBuffer> ReadSourceFrame(BaseBufPool* pBufPool, AL_TBuffer* conversionBuffer, unique_ptr<FrameReader> const& frameReader, AL_TDimension tUpdatedDim, IConvSrc* hConv)
{
  shared_ptr<AL_TBuffer> sourceBuffer = pBufPool->GetSharedBuffer();

  if(sourceBuffer == nullptr)
    throw runtime_error("sourceBuffer must exist");

  if(!ReadSourceFrameBuffer(sourceBuffer.get(), conversionBuffer, frameReader, tUpdatedDim, hConv))
    return nullptr;
  return sourceBuffer;
}

static AL_TPicFormat GetSrcPicFormat(AL_TEncChanParam const& tChParam)
{
  AL_ESrcMode eSrcMode = tChParam.eSrcMode;
  auto eChromaMode = AL_GET_CHROMA_MODE(tChParam.ePicFormat);

  return AL_EncGetSrcPicFormat(eChromaMode, tChParam.uSrcBitDepth, eSrcMode);
}

static bool IsConversionNeeded(SrcConverterParams& tSrcConverterParams)
{

  const TFourCC tSrcFourCC = AL_GetFourCC(tSrcConverterParams.tSrcPicFmt);

  if(tSrcConverterParams.tFileFourCC != tSrcFourCC)
  {
    if(AL_IsCompatible(tSrcConverterParams.tFileFourCC, tSrcFourCC))
      // Update PicFormat to avoid conversion
      AL_GetPicFormat(tSrcConverterParams.tFileFourCC, &tSrcConverterParams.tSrcPicFmt);
    else
      return true;
  }

  return false;
}

static unique_ptr<IConvSrc> AllocateSrcConverter(SrcConverterParams const& tSrcConverterParams, shared_ptr<AL_TBuffer>& pFileReaderYuv)
{
  // ********** Allocate the YUV buffer to read in the file **********
  pFileReaderYuv = AllocateConversionBuffer(tSrcConverterParams.tDim.iWidth, tSrcConverterParams.tDim.iHeight, tSrcConverterParams.tFileFourCC);

  if(pFileReaderYuv == nullptr)
    throw runtime_error("Couldn't allocate source conversion buffer");

  // ************* Allocate the YUV converter *************
  TFrameInfo tSrcFrameInfo = { tSrcConverterParams.tDim, tSrcConverterParams.tSrcPicFmt.uBitDepth, tSrcConverterParams.tSrcPicFmt.eChromaMode };
  (void)tSrcFrameInfo;

  switch(tSrcConverterParams.eSrcFormat)
  {
  case AL_SRC_FORMAT_RASTER:
    return make_unique<CYuvSrcConv>(tSrcFrameInfo);
#ifdef HAVE_VCU2_CTRLSW
  case AL_SRC_FORMAT_RASTER_MSB:
    return make_unique<CYuvSrcConv>(tSrcFrameInfo);
  case AL_SRC_FORMAT_TILE_64x4:
  case AL_SRC_FORMAT_TILE_32x4:
    return make_unique<CYuvSrcConv>(tSrcFrameInfo);
#endif
  default:
    throw runtime_error("Unsupported source conversion.");
  }

  return nullptr;
}

static int32_t ComputeYPitch(int32_t iWidth, const AL_TPicFormat& tPicFormat)
{
  auto iPitch = AL_EncGetMinPitch(iWidth, &tPicFormat);

  if(g_Stride != -1)
  {
    if(g_Stride < iPitch)
      throw runtime_error("g_Stride(" + to_string(g_Stride) + ") must be higher or equal than iPitch(" + to_string(iPitch) + ")");
    iPitch = g_Stride;
  }
  return iPitch;
}

static bool isLastPict(int32_t iPictCount, int32_t iMaxPict)
{
  return (iPictCount >= iMaxPict) && (iMaxPict != -1);
}

static shared_ptr<AL_TBuffer> GetSrcFrame(int& iReadCount, int32_t iPictCount, unique_ptr<FrameReader> const& frameReader, AL_TYUVFileInfo const& FileInfo, PixMapBufPool& SrcBufPool, AL_TBuffer* Yuv, AL_TEncChanParam const& tChParam, ConfigFile const& cfg, IConvSrc* pSrcConv)
{
  shared_ptr<AL_TBuffer> frame;

  if(!isLastPict(iPictCount, cfg.RunInfo.iMaxPict))
  {
    if(cfg.MainInput.FileInfo.FrameRate != tChParam.tRCParam.uFrameRate)
    {
      iReadCount += frameReader->GotoNextPicture(FileInfo.FrameRate, tChParam.tRCParam.uFrameRate, iPictCount, iReadCount);
    }

    auto tUpdatedDim = AL_TDimension {
      AL_GetSrcWidth(tChParam), AL_GetSrcHeight(tChParam)
    };
    frame = ReadSourceFrame(&SrcBufPool, Yuv, frameReader, tUpdatedDim, pSrcConv);

    iReadCount++;
  }
  return frame;
}

static AL_ESrcMode SrcFormatToSrcMode(AL_ESrcFormat eSrcFormat)
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
    throw runtime_error("Unsupported source format.");
  }
}

/*****************************************************************************/
static bool InitQpBufPool(BufPool& pool, AL_TEncSettings& Settings, AL_TEncChanParam& tChParam, int32_t frameBuffersCount, AL_TAllocator* pAllocator)
{
  (void)Settings;

  if(!AL_IS_QP_TABLE_REQUIRED(Settings.eQpTableMode))
    return true;

  AL_TDimension tDim = { tChParam.uEncWidth, tChParam.uEncHeight };
  return pool.Init(pAllocator, frameBuffersCount, AL_GetAllocSizeEP2(tDim, static_cast<AL_ECodec>(AL_GET_CODEC(tChParam.eProfile)), tChParam.uLog2MaxCuSize), nullptr, "qp-ext");
}

/*****************************************************************************/
static SrcBufDesc GetSrcBufDescription(AL_TDimension tDimension, uint8_t uBitDepth, AL_EChromaMode eCMode, AL_ESrcMode eSrcMode, AL_ECodec eCodec)
{
  (void)eCodec;

  AL_TPicFormat const tPicFormat = AL_EncGetSrcPicFormat(eCMode, uBitDepth, eSrcMode);

  SrcBufDesc srcBufDesc =
  {
    AL_GetFourCC(tPicFormat), {}
  };

  int32_t iPitchY = ComputeYPitch(tDimension.iWidth, tPicFormat);

  int32_t iAlignValue = 8;

  int32_t iStrideHeight = g_StrideHeight != -1 ? g_StrideHeight : AL_RoundUp(tDimension.iHeight, iAlignValue);

  SrcBufChunk srcChunk {};

  AL_EPlaneId usedPlanes[AL_MAX_BUFFER_PLANES];
  int32_t iNbPlanes = AL_Plane_GetBufferPixelPlanes(tPicFormat, usedPlanes);

  for(int32_t iPlane = 0; iPlane < iNbPlanes; iPlane++)
  {
    int32_t iPitch = usedPlanes[iPlane] == AL_PLANE_Y ? iPitchY : AL_GetChromaPitch(srcBufDesc.tFourCC, iPitchY);
    srcChunk.vPlaneDesc.push_back(AL_TPlaneDescription { usedPlanes[iPlane], srcChunk.iChunkSize, iPitch });
    srcChunk.iChunkSize += AL_GetAllocSizeSrc_PixPlane(&tPicFormat, iPitchY, iStrideHeight, usedPlanes[iPlane]);

    if(g_MultiChunk)
    {
      srcBufDesc.vChunks.push_back(srcChunk);
      srcChunk = {};
    }
  }

  if(!g_MultiChunk)
    srcBufDesc.vChunks.push_back(srcChunk);

  return srcBufDesc;
}

/*****************************************************************************/
static uint8_t GetNumBufForGop(AL_TEncSettings Settings)
{
  int32_t uNumFields = 1;

  if(AL_IS_INTERLACED(Settings.tChParam[0].eVideoMode))
    uNumFields = 2;
  int32_t uAdditionalBuf = 0;
  return uNumFields * Settings.tChParam[0].tGopParam.uNumB + uAdditionalBuf;
}

/*****************************************************************************/
static bool InitStreamBufPool(BufPool& pool, AL_TEncSettings& Settings, int32_t iLayerID, uint8_t uNumCore, int32_t iForcedStreamBufferSize, AL_TAllocator* pAllocator)
{
  (void)uNumCore;

  int32_t numStreams;

  AL_TDimension dim = { Settings.tChParam[iLayerID].uEncWidth, Settings.tChParam[iLayerID].uEncHeight };
  uint64_t streamSize = iForcedStreamBufferSize;

  if(streamSize == 0)
  {
    streamSize = AL_GetMitigatedMaxNalSize(dim, AL_GET_CHROMA_MODE(Settings.tChParam[0].ePicFormat), AL_GET_BITDEPTH(Settings.tChParam[0].ePicFormat));

    bool bIsXAVCIntraCBG = AL_IS_XAVC_CBG(Settings.tChParam[0].eProfile) && AL_IS_INTRA_PROFILE(Settings.tChParam[0].eProfile);

    if(bIsXAVCIntraCBG)
      streamSize = AL_GetMaxNalSize(dim, AL_GET_CHROMA_MODE(Settings.tChParam[0].ePicFormat), AL_GET_BITDEPTH(Settings.tChParam[0].ePicFormat), Settings.tChParam[0].eProfile, Settings.tChParam[0].uLevel);
  }

  {
    static const int32_t smoothingStream = 2;
    numStreams = g_defaultMinBuffers + smoothingStream + GetNumBufForGop(Settings);
  }

  bool bHasLookAhead;
  bHasLookAhead = AL_TwoPassMngr_HasLookAhead(Settings);

  if(bHasLookAhead)
  {
    int32_t extraLookAheadStream = 1;

    // the look ahead needs one more stream buffer to work in AVC due to (potential) multi-core
    if(AL_IS_AVC(Settings.tChParam[0].eProfile))
      extraLookAheadStream += 1;
    numStreams += extraLookAheadStream;
  }

  if(Settings.tChParam[0].bSubframeLatency)
  {
    numStreams *= Settings.tChParam[0].uNumSlices;

    {
      /* Due to rounding, the slices don't have all the same height. Compute size of the biggest slice */
      uint64_t lcuSize = 1LL << Settings.tChParam[0].uLog2MaxCuSize;
      uint64_t rndHeight = AL_RoundUp(dim.iHeight, lcuSize);
      streamSize = streamSize * lcuSize * (1 + rndHeight / (Settings.tChParam[0].uNumSlices * lcuSize)) / rndHeight;

      /* we need space for the headers on each slice */
      streamSize += AL_ENC_MAX_HEADER_SIZE;
      /* stream size is required to be 32bytes aligned */
      streamSize = AL_RoundUp(streamSize, HW_IP_BURST_ALIGNMENT);
    }
  }

  if(streamSize > INT32_MAX)
    throw runtime_error("streamSize(" + to_string(streamSize) + ") must be lower or equal than INT32_MAX(" + to_string(INT32_MAX) + ")");

  auto pMetaData = (AL_TMetaData*)AL_StreamMetaData_Create(AL_MAX_SECTION);
  bool bSucceed = pool.Init(pAllocator, numStreams, streamSize, pMetaData, "stream");
  AL_MetaData_Destroy(pMetaData);

  return bSucceed;
}

/*****************************************************************************/
static void InitSrcBufPool(PixMapBufPool& SrcBufPool, AL_TAllocator* pAllocator, TFrameInfo& FrameInfo, AL_ESrcMode eSrcMode, int32_t frameBuffersCount, AL_ECodec eCodec)
{
  auto srcBufDesc = GetSrcBufDescription(FrameInfo.tDimension, FrameInfo.iBitDepth, FrameInfo.eCMode, eSrcMode, eCodec);

  SrcBufPool.SetFormat(FrameInfo.tDimension, srcBufDesc.tFourCC);

  for(auto& vChunk : srcBufDesc.vChunks)
    SrcBufPool.AddChunk(vChunk.iChunkSize, vChunk.vPlaneDesc);

  bool const ret = SrcBufPool.Init(pAllocator, frameBuffersCount, "input");

  if(!ret)
    throw std::runtime_error("src buf pool must succeed init");
}

/*****************************************************************************/
void LayerResources::Init(ConfigFile& cfg, AL_TEncoderInfo tEncInfo, int32_t iLayerID, CIpDevice* pDevices, int32_t chanId)
{
  AL_TEncSettings& Settings = cfg.Settings;
  auto const eSrcMode = Settings.tChParam[iLayerID].eSrcMode;

  (void)chanId;
  this->iLayerID = iLayerID;

  {
    layerInputs.push_back(cfg.MainInput);
    layerInputs.insert(layerInputs.end(), cfg.DynamicInputs.begin(), cfg.DynamicInputs.end());
  }

  AL_TAllocator* pAllocator = pDevices->GetAllocator();

  // --------------------------------------------------------------------------------
  // Stream Buffers
  // --------------------------------------------------------------------------------
  if(!InitStreamBufPool(StreamBufPool, Settings, iLayerID, tEncInfo.uNumCore, cfg.iForceStreamBufSize, pAllocator))
    throw std::runtime_error("Error creating stream buffer pool");

  AL_TDimension tDim = { Settings.tChParam[iLayerID].uEncWidth, Settings.tChParam[iLayerID].uEncHeight };

  bool bUsePictureMeta = false;
  bUsePictureMeta |= cfg.RunInfo.printPictureType;

  bUsePictureMeta |= AL_TwoPassMngr_HasLookAhead(Settings);

  if(iLayerID == 0 && bUsePictureMeta)
  {
    auto pMeta = (AL_TMetaData*)AL_PictureMetaData_Create();

    if(pMeta == nullptr)
      throw std::runtime_error("Meta must be created");
    bool const bRet = StreamBufPool.AddMetaData(pMeta);

    if(!bRet)
      throw std::runtime_error("Meta must be added in stream pool");
    AL_MetaData_Destroy(pMeta);
  }

  if(cfg.RunInfo.rateCtrlStat != AL_RATECTRL_STAT_MODE_NONE)
  {
    auto pMeta = (AL_TMetaData*)AL_RateCtrlMetaData_CustomCreate(pAllocator, cfg.RunInfo.rateCtrlStat, tDim, Settings.tChParam[iLayerID].uLog2MaxCuSize, AL_GET_CODEC(Settings.tChParam[iLayerID].eProfile));

    if(pMeta == nullptr)
      throw std::runtime_error("Meta must be created");
    bool const bRet = StreamBufPool.AddMetaData(pMeta);

    if(!bRet)
      throw std::runtime_error("Meta must be added in stream pool");

    AL_MetaData_Destroy(pMeta);
  }

  // --------------------------------------------------------------------------------
  // Tuning Input Buffers
  // --------------------------------------------------------------------------------
  int32_t frameBuffersCount = g_defaultMinBuffers + GetNumBufForGop(Settings);

  {
    frameBuffersCount = g_defaultMinBuffers + GetNumBufForGop(Settings);

    if(AL_TwoPassMngr_HasLookAhead(Settings))
    {
      frameBuffersCount += Settings.LookAhead + (GetNumBufForGop(Settings) * 2);

      if(AL_IS_AVC(cfg.Settings.tChParam[0].eProfile))
        frameBuffersCount += 1;
    }

  }

  if(!InitQpBufPool(QpBufPool, Settings, Settings.tChParam[iLayerID], frameBuffersCount, pAllocator))
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

  if(IsConversionNeeded(tSrcConverterParams))
    pSrcConv = AllocateSrcConverter(tSrcConverterParams, SrcYuv);

  TFrameInfo tSrcFrameInfo = { tSrcConverterParams.tDim, tSrcConverterParams.tSrcPicFmt.uBitDepth, tSrcConverterParams.tSrcPicFmt.eChromaMode };

  // --------------------------------------------------------------------------------
  // Source Buffers
  // --------------------------------------------------------------------------------
  int32_t srcBuffersCount = max(frameBuffersCount, g_numFrameToRepeat);

  InitSrcBufPool(SrcBufPool, pAllocator, tSrcFrameInfo, eSrcMode, srcBuffersCount, static_cast<AL_ECodec>(AL_GET_CODEC(Settings.tChParam[0].eProfile)));

  iPictCount = 0;
  iReadCount = 0;
}

void LayerResources::PushResources(ConfigFile& cfg, EncoderSink* enc
                                   , EncoderLookAheadSink* encFirstPassLA
                                   )
{
  (void)cfg;
  QPBuffers::QPLayerInfo qpInf
  {
    &QpBufPool,
    layerInputs[iInputIdx].sQPTablesFolder,
    layerInputs[iInputIdx].sRoiFileName
  };

  enc->AddQpBufPool(qpInf, iLayerID);

  if(AL_TwoPassMngr_HasLookAhead(cfg.Settings))
  {
    encFirstPassLA->AddQpBufPool(qpInf, iLayerID);
  }

  if(frameWriter)
    enc->RecOutput[iLayerID] = std::move(frameWriter);

  for(int32_t i = 0; i < (int)StreamBufPool.GetNumBuf(); ++i)
  {
    std::shared_ptr<AL_TBuffer> pStream = StreamBufPool.GetSharedBuffer(AL_EBufMode::AL_BUF_MODE_NONBLOCK);

    if(pStream == nullptr)
      throw runtime_error("pStream must exist");

    AL_HEncoder hEnc = enc->hEnc;

    bool bRet = true;

    if(iLayerID == 0)
    {
      int32_t iStreamNum = 1;

      // the look ahead needs one more stream buffer to work AVC due to (potential) multi-core
      if(AL_IS_AVC(cfg.Settings.tChParam[0].eProfile))
        iStreamNum += 1;

      if(AL_TwoPassMngr_HasLookAhead(cfg.Settings) && i < iStreamNum)
        hEnc = encFirstPassLA->hEnc;

      bRet = AL_Encoder_PutStreamBuffer(hEnc, pStream.get());
    }

    if(!bRet)
      throw std::runtime_error("bRet must be true");
  }
}

void LayerResources::OpenEncoderInput(ConfigFile& cfg, AL_HEncoder hEnc)
{
  ChangeInput(cfg, iInputIdx, hEnc);
}

bool LayerResources::SendInput(ConfigFile& cfg, IFrameSink* firstSink, void* pTraceHooker)
{
  (void)pTraceHooker;
  firstSink->PreprocessFrame();

  return sendInputFileTo(frameReader, SrcBufPool, SrcYuv.get(), cfg, layerInputs[iInputIdx].FileInfo, pSrcConv.get(), firstSink, iPictCount, iReadCount);
}

bool LayerResources::sendInputFileTo(unique_ptr<FrameReader>& frameReader, PixMapBufPool& SrcBufPool, AL_TBuffer* Yuv, ConfigFile const& cfg, AL_TYUVFileInfo& FileInfo, IConvSrc* pSrcConv, IFrameSink* sink, int& iPictCount, int& iReadCount)
{
  if(AL_IS_ERROR_CODE(GetEncoderLastError()))
  {
    sink->ProcessFrame(nullptr);
    return false;
  }

  shared_ptr<AL_TBuffer> frame = GetSrcFrame(iReadCount, iPictCount, frameReader, FileInfo, SrcBufPool, Yuv, cfg.Settings.tChParam[0], cfg, pSrcConv);

  sink->ProcessFrame(frame.get());

  if(!frame)
    return false;

  iPictCount++;
  return true;
}

unique_ptr<FrameReader> LayerResources::InitializeFrameReader(ConfigFile& cfg, ifstream& YuvFile, string sYuvFileName, ifstream& MapFile, string sMapFileName, AL_TYUVFileInfo& FileInfo)
{
  (void)(MapFile);

  unique_ptr<FrameReader> pFrameReader;
  bool bUseCompressedFormat = AL_IsCompressed(FileInfo.FourCC);
  bool bHasCompressionMapFile = !sMapFileName.empty();

  if(bUseCompressedFormat != bHasCompressionMapFile)
    throw runtime_error(std::string("Providing a map file is ") + std::string(bUseCompressedFormat ? "mandatory" : "forbidden") +
                        " when using " + std::string(bUseCompressedFormat ? "compressed" : "uncompressed") + " input.");

  YuvFile.close();
  OpenInput(YuvFile, sYuvFileName);

  if(!bUseCompressedFormat)
    pFrameReader = unique_ptr<FrameReader>(new UnCompFrameReader(YuvFile, FileInfo, cfg.RunInfo.bLoop));
#ifdef HAVE_VCU2_CTRLSW
  pFrameReader->SeekA(cfg.RunInfo.iFirstPict + iReadCount);
#elif defined(HAVE_VCU_CTRLSW)
  pFrameReader->SeekAbsolute(cfg.RunInfo.iFirstPict + iReadCount);
#endif

  return pFrameReader;
}

void LayerResources::ChangeInput(ConfigFile& cfg, int32_t iInputIdx, AL_HEncoder hEnc)
{
  (void)hEnc;

  if(iInputIdx < static_cast<int>(layerInputs.size()))
  {
    this->iInputIdx = iInputIdx;
    AL_TDimension inputDim = { layerInputs[iInputIdx].FileInfo.PictWidth, layerInputs[iInputIdx].FileInfo.PictHeight };
    bool bResChange = (inputDim.iWidth != AL_GetSrcWidth(cfg.Settings.tChParam[iLayerID])) || (inputDim.iHeight != AL_GetSrcHeight(cfg.Settings.tChParam[iLayerID]));

    if(bResChange)
    {
      /* No resize with dynamic resolution changes */
      cfg.Settings.tChParam[iLayerID].uEncWidth = cfg.Settings.tChParam[iLayerID].uSrcWidth = inputDim.iWidth;
      cfg.Settings.tChParam[iLayerID].uEncHeight = cfg.Settings.tChParam[iLayerID].uSrcHeight = inputDim.iHeight;

      AL_Encoder_SetInputResolution(hEnc, inputDim);
    }

    frameReader = InitializeFrameReader(cfg, YuvFile,
                                        layerInputs[iInputIdx].YUVFileName,
                                        MapFile,
                                        cfg.MainInput.sMapFileName,
                                        layerInputs[iInputIdx].FileInfo);

  }
}

static unique_ptr<EncoderSink> ChannelMain(ConfigFile& cfg, vector<unique_ptr<LayerResources>>& pLayerResources,
                CIpDevice* pIpDevice, CIpDeviceParam& param, int32_t chanId)
{
  (void)param;
  auto& Settings = cfg.Settings;
  auto& StreamFileName = cfg.BitstreamFileName;
  auto& RunInfo = cfg.RunInfo;

  /* null if not supported */
  //void* pTraceHook {};
  unique_ptr<EncoderSink> enc;
  unique_ptr<EncoderLookAheadSink> encFirstPassLA;

  auto pAllocator = pIpDevice->GetAllocator();
  auto pScheduler = pIpDevice->GetScheduler();

#ifdef HAVE_VCU2_CTRLSW
  auto ctx = pIpDevice->GetCtx();
#endif

  AL_EVENT hFinished = Rtos_CreateEvent(false);
  RCPlugin_Init(&cfg.Settings, &cfg.Settings.tChParam[0], pAllocator);

  auto OnScopeExit = scopeExit([&]() {
    Rtos_DeleteEvent(hFinished);
    AL_Allocator_Free(pAllocator, cfg.Settings.hRcPluginDmaContext);
  });

  // --------------------------------------------------------------------------------
  // Create Encoder

#ifdef HAVE_VCU2_CTRLSW
  if(ctx)
    enc.reset(new EncoderSink(cfg, ctx, pAllocator));
  else
#endif
  enc.reset(new EncoderSink(cfg, pScheduler, pAllocator));

  IFrameSink* firstSink = enc.get();

  if(AL_TwoPassMngr_HasLookAhead(cfg.Settings))
  {

#ifdef HAVE_VCU2_CTRLSW
    if(ctx)
      encFirstPassLA.reset(new EncoderLookAheadSink(cfg, ctx, pAllocator));
    else
#endif
    encFirstPassLA.reset(new EncoderLookAheadSink(cfg, pScheduler, pAllocator));

    encFirstPassLA->next = firstSink;
    firstSink = encFirstPassLA.get();
  }

  // --------------------------------------------------------------------------------
  // Allocate/Push Layers resources
  AL_TEncoderInfo tEncInfo;
  AL_Encoder_GetInfo(enc->hEnc, &tEncInfo);

  for(size_t i = 0; i < pLayerResources.size(); i++)
  {
    auto multisinkRec = unique_ptr<MultiSink>(new MultiSink);
    pLayerResources[i]->Init(cfg, tEncInfo, i, pIpDevice, chanId);
    pLayerResources[i]->PushResources(cfg, enc.get()
                                    ,
                                    encFirstPassLA.get()
                                    );

    // Rec file creation
    string LayerRecFileName = cfg.RecFileName;

    if(!LayerRecFileName.empty())
    {

#ifdef HAVE_VCU2_CTRLSW
      if(Settings.tChParam[0].eEncOptions & AL_OPT_COMPRESS)
      {
        std::unique_ptr<IFrameSink> recOutput(createCompFrameSink(LayerRecFileName, LayerRecFileName + ".map", Settings.tChParam[0].eRecStorageMode, 0));
        multisinkRec->addSink(recOutput);
      }
      else
      {
        std::unique_ptr<IFrameSink> recOutput(createUnCompFrameSink(LayerRecFileName, AL_FB_RASTER));
        multisinkRec->addSink(recOutput);
      }
#elif defined(HAVE_VCU_CTRLSW)
      std::unique_ptr<IFrameSink> recOutput(createUnCompFrameSink(LayerRecFileName, AL_FB_RASTER));
      multisinkRec->addSink(recOutput);
#endif
    }
    enc->RecOutput[i] = std::move(multisinkRec);
  }

  auto multisink = unique_ptr<MultiSink>(new MultiSink);

  std::unique_ptr<IFrameSink> bitstreamOutput(createBitstreamWriter(StreamFileName, cfg));
  multisink->addSink(bitstreamOutput);

  if(!RunInfo.sStreamMd5Path.empty())
  {
    std::unique_ptr<IFrameSink> md5Calculator(createStreamMd5Calculator(RunInfo.sStreamMd5Path));
    multisink->addSink(md5Calculator);
  }

  if(!RunInfo.bitrateFile.empty())
  {
    std::unique_ptr<IFrameSink> bitrateOutput(createBitrateWriter(RunInfo.bitrateFile, cfg));
    multisink->addSink(bitrateOutput);
  }

  if(RunInfo.rateCtrlStat != AL_RATECTRL_STAT_MODE_NONE && !RunInfo.rateCtrlMetaPath.empty())
  {
    std::unique_ptr<IFrameSink> rateCtrlMetaSink(createRateCtrlMetaSink(RunInfo.rateCtrlMetaPath));
    multisink->addSink(rateCtrlMetaSink);
  }

  enc->BitstreamOutput[0] = std::move(multisink);

  // --------------------------------------------------------------------------------
  // Set Callbacks
  enc->m_InputChanged = ([&](int32_t iInputIdx, int32_t iLayerID) {
    pLayerResources[iLayerID]->ChangeInput(cfg, iInputIdx, enc->hEnc);
  });

  enc->m_done = ([&]() {
    Rtos_SetEvent(hFinished);
  });

  if(!RunInfo.sRecMd5Path.empty())
  {
    for(int32_t iLayerID = 0; iLayerID < Settings.NumLayer; ++iLayerID)
    {
      auto layer_multisink = unique_ptr<MultiSink>(new MultiSink);
      layer_multisink->addSink(enc->RecOutput[iLayerID]);
      string LayerMd5FileName = RunInfo.sRecMd5Path;
      std::unique_ptr<IFrameSink> md5Calculator(createYuvMd5Calculator(LayerMd5FileName, cfg));
      layer_multisink->addSink(md5Calculator);
      enc->RecOutput[iLayerID] = std::move(layer_multisink);
    }
  }

  unique_ptr<RepeaterSink> prefetch;

  if(g_numFrameToRepeat > 0)
  {
    prefetch.reset(new RepeaterSink(g_numFrameToRepeat, RunInfo.iMaxPict));
    prefetch->next = firstSink;
    firstSink = prefetch.get();
    RunInfo.iMaxPict = g_numFrameToRepeat;
  }

#if 0 // encoder input file is not needed for opencv case
  for(int32_t i = 0; i < Settings.NumLayer; ++i)
    pLayerResources[i]->OpenEncoderInput(cfg, enc->hEnc);
#endif

  return enc;
}

/*****************************************************************************/
unique_ptr<EncoderSink> CtrlswEncOpen(ConfigFile& cfg, std::vector<std::unique_ptr<LayerResources>>& pLayerResources, shared_ptr<CIpDevice>& pIpDevice)
{
  unique_ptr<EncoderSink> enc;
  InitializePlateform();

  {
    auto& Settings = cfg.Settings;
    auto& RecFileName = cfg.RecFileName;
    auto& RunInfo = cfg.RunInfo;

    AL_Settings_SetDefaultParam(&Settings);
    SetMoreDefaults(cfg);

    if(!RecFileName.empty() || !RunInfo.sRecMd5Path.empty())
    {
      Settings.tChParam[0].eEncOptions = (AL_EChEncOption)(Settings.tChParam[0].eEncOptions | AL_OPT_FORCE_REC);
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

  auto& RunInfo = cfg.RunInfo;

#ifdef HAVE_VCU2_CTRLSW
  if(RunInfo.eDeviceType == AL_EDeviceType::AL_DEVICE_TYPE_EMBEDDED)
    eArch = AL_LIB_ENCODER_ARCH_RISCV;
#endif

  if(!AL_IS_SUCCESS_CODE(AL_Lib_Encoder_Init(eArch)))
    throw runtime_error("Can't setup encode library");

  CIpDeviceParam param;
  param.eSchedulerType = RunInfo.eSchedulerType;
  param.eDeviceType = RunInfo.eDeviceType;

  param.pCfgFile = &cfg;
#ifdef HAVE_VCU2_CTRLSW
  param.bTrackDma = RunInfo.trackDma;
#elif defined(HAVE_VCU_CTRLSW)
  param.eTrackDmaMode = RunInfo.eTrackDmaMode;
#endif

  pIpDevice = shared_ptr<CIpDevice>(new CIpDevice);

  if(!pIpDevice)
    throw runtime_error("Can't create IpDevice");

  pIpDevice->Configure(param);

  enc = ChannelMain(cfg, pLayerResources, pIpDevice.get(), param, 0);

  return enc;
}

#endif // HAVE_VCU_CTRLSW || HAVE_VCU2_CTRLSW
