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

#include "ctrlsw_dec.hpp"

#include <opencv2/core.hpp>

namespace cv {
namespace vcucodec {

/*****************************************************************************/
/*******class DecCIpDevice ***************************************************/
/*****************************************************************************/
void DecCIpDevice::ConfigureRiscv(void)
{
  uint32_t const sw_version =
      (uint32_t)((AL_VERSION_MAJOR << 20) + (AL_VERSION_MINOR << 12) + (AL_VERSION_PATCH));
  uint32_t fw_version;

  m_pScheduler = nullptr;
  m_ctx = AL_Riscv_Decode_CreateCtx(this->m_tSelectedDevice.c_str());

  if(!m_ctx)
    throw std::runtime_error("Failed to create context (trying to use " + this->m_tSelectedDevice + ")");

  fw_version = AL_Riscv_Decode_Get_FwVersion(m_ctx);

  if(!fw_version || (fw_version != sw_version))
    throw std::runtime_error("FW Version " + VersionToStr(fw_version) + ", it should be "
                             + VersionToStr(sw_version));

  AL_TAllocator* pRiscvAllocator = AL_Riscv_Decode_DmaAlloc_Create(m_ctx);
  m_pTimer = nullptr;

  if(!pRiscvAllocator)
    throw std::runtime_error("Can't find dma allocator");

  m_pAllocator.reset(pRiscvAllocator, &AL_Allocator_Destroy);
}

DecCIpDevice::~DecCIpDevice(void)
{
  if(m_pScheduler)
    AL_IDecScheduler_Destroy(m_pScheduler);

  if(m_pTimer)
    AL_ITimer_Deinit(m_pTimer);

  if(m_ctx)
    AL_Riscv_Decode_DestroyCtx(m_ctx);

}

DecCIpDevice::DecCIpDevice(DecCIpDeviceParam const& param, AL_EDeviceType eDeviceType,
    std::set<std::string> tDevices) : m_tDevices(tDevices)
{
  (void)param;
  this->m_eDeviceType = eDeviceType;

  if(m_eDeviceType == AL_EDeviceType::AL_DEVICE_TYPE_EMBEDDED)
  {
    if(this->m_tDevices.size() != 1)
      throw std::runtime_error("Embedded device doesn't support multi devices");
    this->m_tSelectedDevice = *m_tDevices.begin();
    ConfigureRiscv();
    return;
  }

  throw std::runtime_error("No support for this scheduling type");
}

AL_EDeviceType DecCIpDevice::GetDeviceType(void)
{
  return this->m_eDeviceType;
}

/*****************************************************************************/
/*******class Config *********************************************************/
/*****************************************************************************/
static AL_TDecSettings GetDefaultDecSettings(void)
{
  AL_TDecSettings settings {};
  AL_DecSettings_SetDefaults(&settings);
  settings.uNumBuffersHeldByNextComponent = uDefaultNumBuffersHeldByNextComponent;
  return settings;
}

Config::Config(void)
{
  tDecSettings = GetDefaultDecSettings();
}

/******************************************************************************/
/*******class DisplayManager **************************************************/
/******************************************************************************/
AL_EFbStorageMode GetMainOutputStorageMode(AL_TDecOutputSettings tUserOutputSettings,
    AL_EFbStorageMode eOutstorageMode)
{
  (void)tUserOutputSettings;
  AL_EFbStorageMode eOutputStorageMode = eOutstorageMode;

  if(tUserOutputSettings.bCustomFormat)
  {
    if(tUserOutputSettings.tPicFormat.eStorageMode != AL_FB_MAX_ENUM)
      eOutputStorageMode = tUserOutputSettings.tPicFormat.eStorageMode;
    else
      eOutputStorageMode = AL_FB_RASTER;
  }

  return eOutputStorageMode;
}

void DisplayManager::Configure(Config const& config)
{
  if(config.tOutputFourCC != FOURCC(NULL))
    eMainOutputStorageMode = AL_GetStorageMode(config.tOutputFourCC);
  else
  {
    eMainOutputStorageMode = config.tDecSettings.eFBStorageMode;

    if(config.tUserOutputSettings.tPicFormat.eStorageMode != AL_FB_MAX_ENUM)
      eMainOutputStorageMode = GetMainOutputStorageMode(config.tUserOutputSettings,
                                                        config.tDecSettings.eFBStorageMode);

     eMainOutputStorageMode = AL_FB_RASTER;
  }

  bHasOutput = config.bEnableYUVOutput;
  bEnableYuvOutput = config.bEnableYUVOutput;

  if(bHasOutput)
  {
    if(config.bEnableYUVOutput)
    {
      hFileOut.reset(new ofstream(config.sMainOut, ios::binary));

      if(!hFileOut->is_open())
        CV_Error(cv::Error::StsBadArg, "Invalid output file");
    }
  }

  iBitDepth = config.iOutputBitDepth;
  tOutputFourCC = config.tOutputFourCC;
  uMaxFrames = config.iMaxFrames;

  //std::unique_ptr<IFrameSink> crcDump(createStreamCrcDump(config.sCrc));
  //multisinkRaw->addSink(crcDump);
}

void DisplayManager::ConfigureMainOutputWriters(AL_TDecOutputSettings const& tDecOutputSettings)
{
  (void)tDecOutputSettings;

  if(!bHasOutput || bOutputWritersCreated || !bEnableYuvOutput)
    return;

  AL_EOutputType eOutputType = AL_OUTPUT_MAIN;
  AL_EFbStorageMode eOutputStorageMode = eMainOutputStorageMode;

  std::unique_ptr<IFrameSink> frameSink(createUnCompFrameSink(hFileOut, eOutputStorageMode));
  std::unique_ptr<IFrameSink> uncompressedSink(new SinkFilter(eOutputType, frameSink));
  multisinkOut->addSink(uncompressedSink);

  bOutputWritersCreated = true;
}

static void sFreeWithoutDestroyingMemory(AL_TBuffer* buffer)
{
  buffer->iChunkCnt = 0;
  AL_Buffer_Destroy(buffer);
}

void DisplayManager::CopyMetaData(AL_TBuffer* pDstFrame, AL_TBuffer* pSrcFrame, AL_EMetaType eMetaType)
{
  AL_TMetaData* pMetaD = nullptr;

  AL_TMetaData* pOrigMeta = AL_Buffer_GetMetaData(pSrcFrame, eMetaType);

  if(!pOrigMeta)
    throw runtime_error("Metadata is NULL");
  switch(eMetaType)
  {
  case AL_META_TYPE_PIXMAP:
  {
    pMetaD = (AL_TMetaData*)AL_PixMapMetaData_Clone((AL_TPixMapMetaData*)pOrigMeta);
    break;
  }
  case AL_META_TYPE_DISPLAY_INFO:
  {
    pMetaD = (AL_TMetaData*)AL_DisplayInfoMetaData_Clone((AL_TDisplayInfoMetaData*)pOrigMeta);
    break;
  }
  default:
    throw std::runtime_error("Metadata type is not supported");
    break;
  }

  if(pMetaD == NULL)
    throw runtime_error("Clone of MetaData was not created!");

  if(!AL_Buffer_AddMetaData(pDstFrame, pMetaD))
    throw runtime_error("Cloned pMetaD did not get added!\n");
}

bool DisplayManager::Process(Ptr<Frame> frame, int32_t iBitDepthAlloc, bool& bIsMainDisplay,
                             bool& bNumFrameReached, bool bDecoderExists)
{
  AL_TBuffer* pFrame = frame->getBuffer();
  bNumFrameReached = false;
  bIsMainDisplay = frame->isMainOutput();

  if(bDecoderExists)
  {
    if(uNumFrames < uMaxFrames)
    {
      if(!AL_Buffer_GetData(pFrame))
        throw runtime_error("Data buffer is null");

      Ptr<Frame> pDFrame = Frame::createShallowCopy(frame);

      int32_t iCurrentBitDepth = max(frame->bitDepthY(), frame->bitDepthUV());

      if(iBitDepth == OUTPUT_BD_FIRST)
        iBitDepth = iCurrentBitDepth;
      else if(iBitDepth == OUTPUT_BD_ALLOC)
        iBitDepth = iBitDepthAlloc;

      int32_t iEffectiveBitDepth = iBitDepth == OUTPUT_BD_STREAM ? iCurrentBitDepth : iBitDepth;

      if(bHasOutput)
        ProcessFrame(pDFrame, iEffectiveBitDepth, tOutputFourCC);

      if(bIsMainDisplay)
      {
        // TODO: increase only when last frame
        //DisplayFrameStatus(uNumFrames);
      }
    }

    if(bIsMainDisplay)
      uNumFrames++;
  }

  if(uNumFrames >= uMaxFrames)
    bNumFrameReached = true;

  return bNumFrameReached;
}


int32_t convertBitDepthToEven(int32_t iBd)
{
  return ((iBd % 2) != 0) ? iBd + 1 : iBd;
}

Ptr<Frame> DisplayManager::ConvertFrameBuffer(Ptr<Frame> frame, int32_t iBdOut,
    AL_TPosition const& tPos, TFourCC tOutFourCC)
{
  AL_TBuffer* pInput = frame->getBuffer();
  TFourCC tRecFourCC = AL_PixMapBuffer_GetFourCC(pInput);
  AL_TDimension tRecDim = AL_PixMapBuffer_GetDimension(pInput);
  AL_EChromaMode eRecChromaMode = AL_GetChromaMode(tRecFourCC);

  TFourCC tConvFourCC = tOutFourCC;
  AL_TPicFormat tConvPicFormat;
  assert(tConvFourCC);

  AL_PixMapBuffer_SetDimension(pInput, { tPos.iX + tRecDim.iWidth, tPos.iY + tRecDim.iHeight });

  AL_TDimension tDim = AL_PixMapBuffer_GetDimension(pInput);
  Ptr<Frame> pFrame = Frame::createYuvIO({tDim.iWidth, tDim.iHeight}, tConvFourCC);
  AL_TBuffer* pOutput = pFrame->getBuffer();

  if(ConvertPixMapBuffer(pInput, pOutput))
    throw runtime_error("Couldn't convert buffer");

  AL_PixMapBuffer_SetDimension(pInput, tRecDim);
  CopyMetaData(pOutput, pInput, AL_META_TYPE_DISPLAY_INFO);
  return pFrame;
}

Ptr<Frame> DisplayManager::Dequeue(std::chrono::milliseconds timeout)
{
  return frame_queue_.dequeue(timeout);
}

void DisplayManager::ProcessFrame(Ptr<Frame> frame, int32_t iBdOut, TFourCC tOutFourCC)
{
  AL_TBuffer& tRecBuf = *frame->getBuffer();
  AL_TInfoDecode const& info = frame->getInfo();
  AL_PixMapBuffer_SetDimension(&tRecBuf, frame->getDimension());

  iBdOut = convertBitDepthToEven(iBdOut);

  AL_TCropInfo tCrop {};
  tCrop = frame->getCropInfo();
  AL_TPosition tPos = { 0, 0 };

  TFourCC tRecBufFourCC = frame->getFourCC();
  AL_TPicFormat tRecPicFormat;
  AL_GetPicFormat(tRecBufFourCC, &tRecPicFormat);

  bool bNewInputFourCCFound = false;

  if(tInputFourCC != tRecBufFourCC)
  {
    bNewInputFourCCFound = true;
    tInputFourCC = tRecBufFourCC;
  }

  if(tOutFourCC == FOURCC(NULL))
  {
    AL_EPlaneMode ePlaneMode = AL_PLANE_MODE_PLANAR;

    if(tRecPicFormat.bMSB && (tRecPicFormat.eChromaMode == AL_CHROMA_4_2_0
                              || tRecPicFormat.eChromaMode == AL_CHROMA_4_2_2))
      ePlaneMode = AL_PLANE_MODE_SEMIPLANAR;

    AL_TPicFormat tConvPicFormat = AL_TPicFormat {
      tRecPicFormat.eChromaMode,
      AL_ALPHA_MODE_DISABLED,
      static_cast<uint8_t>(iBdOut),
      AL_FB_RASTER,
      ePlaneMode,
      AL_COMPONENT_ORDER_YUV,
      AL_SAMPLE_PACK_MODE_BYTE,
      false,
      tRecPicFormat.bMSB
    };

    tOutFourCC = AL_GetFourCC(tConvPicFormat);
  }
  else if(tOutFourCC == FOURCC(hard))
  {
    tOutFourCC = tRecBufFourCC;
  }

  bool bCompress = AL_IsCompressed(tRecBufFourCC);
  bool bConvert = !bCompress && tOutFourCC != tRecBufFourCC;

  AL_TDisplayInfoMetaData* pMeta = reinterpret_cast<AL_TDisplayInfoMetaData*>(
      AL_Buffer_GetMetaData(&tRecBuf, AL_META_TYPE_DISPLAY_INFO));

  if(pMeta)
    pMeta->tCrop = tCrop;

  if(bConvert)
  {
    if(tInputFourCC != tOutFourCC && bNewInputFourCCFound)
    {
      stringstream ss;
      ss << "Software conversion done from " << AL_FourCCToString(tRecBufFourCC).cFourcc
         << " to " << AL_FourCCToString(tOutFourCC).cFourcc << endl;
      LogInfo(CC_DARK_BLUE, "%s\n", ss.str().c_str());
      //FFLUSH(stdout);
    }
    Ptr<Frame> pYuvFrame = ConvertFrameBuffer(frame, iBdOut, tPos, tOutFourCC);

    frame_queue_.enqueue(pYuvFrame);
  }
  else
  {
    //change to Enqueue later
    multisinkOut->ProcessFrame(&tRecBuf);
  }

}

/******************************************************************************/
/*******class DecoderContext **************************************************/
/******************************************************************************/
DecoderContext::DecoderContext(Config& config, AL_TAllocator* pAlloc)
{
  pAllocator = pAlloc;
  pDecSettings = &config.tDecSettings;
  pUserOutputSettings = &config.tUserOutputSettings;
  tDisplayManager.Configure(config);
  running = false;
  eos = false;

  eExitCondition = config.eExitCondition;
  hExitMain = Rtos_CreateEvent(false);
}

DecoderContext::~DecoderContext(void)
{
  if(ctrlswThread.joinable())
    ctrlswThread.join();
  Rtos_DeleteEvent(hExitMain);
}

AL_HANDLE DecoderContext::GetDecoderHandle() const
{
  AL_HANDLE h = hBaseDec;

  return h;
}

bool DecoderContext::WaitExit(uint32_t uTimeout)
{
  return Rtos_WaitEvent(hExitMain, uTimeout);
}

/* duplicated from Utils.h as we can't take these from inside the libraries */
static inline int32_t RoundUp(int32_t iVal, int32_t iRnd)
{
  return (iVal + iRnd - 1) / iRnd * iRnd;
}

AL_TDimension DecoderContext::ComputeBaseDecoderFinalResolution(AL_TStreamSettings const* pStreamSettings)
{
  AL_TDimension tOutputDim = pStreamSettings->tDim;

  /* For pre-allocation, we must use 8x8 (HEVC) or MB (AVC) rounded dimensions, like the SPS. */
  /* Actually, round up to the LCU so we're able to support resolution changes with the same LCU sizes. */
  /* And because we don't know the codec here, always use 64 as MB/LCU size. */
  tOutputDim.iWidth = RoundUp(tOutputDim.iWidth, 64);
  tOutputDim.iHeight = RoundUp(tOutputDim.iHeight, 64);

  return tOutputDim;
}

static int32_t sConfigureDecBufPool(PixMapBufPool& SrcBufPool, AL_TPicFormat const& tPicFormat,
    AL_TDimension const& tDim, int32_t iPitchY, bool bConfigurePlanarAndSemiplanar)
{
  auto const tFourCC = AL_GetFourCC(tPicFormat);
  SrcBufPool.SetFormat(tDim, tFourCC);

  std::vector<AL_TPlaneDescription> vPlaneDesc;
  int32_t iOffset = 0;

  AL_EPlaneId usedPlanes[AL_MAX_BUFFER_PLANES];
  int32_t iNbPlanes = AL_Plane_GetBufferPixelPlanes(tPicFormat, usedPlanes);

  // Set pixels planes
  // -----------------
  for(int32_t iPlane = 0; iPlane < iNbPlanes; iPlane++)
  {
    int32_t iPitch = (usedPlanes[iPlane] == AL_PLANE_Y || usedPlanes[iPlane] == AL_PLANE_YUV)
                   ? iPitchY : AL_GetChromaPitch(tFourCC, iPitchY);
    vPlaneDesc.push_back(AL_TPlaneDescription { usedPlanes[iPlane], iOffset, iPitch });

    /* We ensure compatibility with 420/422. Only required when we use prealloc configured for
     * 444 chroma-mode (worst case) and the real chroma-mode is unknown. Breaks planes agnostic
     * allocation. */

    if(bConfigurePlanarAndSemiplanar && usedPlanes[iPlane] == AL_PLANE_U)
      vPlaneDesc.push_back(AL_TPlaneDescription { AL_PLANE_UV, iOffset, iPitch });

    iOffset += AL_DecGetAllocSize_Frame_PixPlane(&tPicFormat, tDim, iPitch, usedPlanes[iPlane]);
  }

  SrcBufPool.AddChunk(iOffset, vPlaneDesc);

  return iOffset;
}

int32_t DecoderContext::ComputeBaseDecoderRecBufferSizing(
    AL_TStreamSettings const* pStreamSettings, AL_TDecOutputSettings const* pUserOutputSettings)
{
  // Up to this point pUserOutputSettings is already updated in the resolution found callback
  // (SetupBaseDecoderPool)
  int32_t iBufferSize = 0;

  // Compute output resolution
  AL_TDimension tOutputDim = ComputeBaseDecoderFinalResolution(pStreamSettings);

  // Buffer sizing
  auto minPitch = AL_Decoder_GetMinPitch(tOutputDim.iWidth, &pUserOutputSettings->tPicFormat);

  bool bConfigurePlanarAndSemiplanar = bUsePreAlloc;
  iBufferSize = sConfigureDecBufPool(tBaseBufPool, pUserOutputSettings->tPicFormat, tOutputDim,
                                     minPitch, bConfigurePlanarAndSemiplanar);

  return iBufferSize;
}

void DecoderContext::AttachMetaDataToBaseDecoderRecBuffer(AL_TStreamSettings const* pStreamSettings,
                                                          AL_TBuffer* pDecPict)
{
  (void)pStreamSettings;

  AL_TPictureDecMetaData* pPictureDecMeta = AL_PictureDecMetaData_Create();
  AL_Buffer_AddMetaData(pDecPict, (AL_TMetaData*)pPictureDecMeta);

  AL_TDisplayInfoMetaData* pDisplayInfoMeta = AL_DisplayInfoMetaData_Create();
  AL_Buffer_AddMetaData(pDecPict, (AL_TMetaData*)pDisplayInfoMeta);

}

/* Update picture format using stream settings and decoder's settings*/
static void SetDecOutputSettings(AL_TDecOutputSettings& tUserOutputSettings,
    AL_TStreamSettings const& tStreamSettings, AL_TDecSettings const& tDecSettings)
{
  AL_TPicFormat& tPicFormat = tUserOutputSettings.tPicFormat;

  /* Chroma mode */
  if(AL_CHROMA_MAX_ENUM == tPicFormat.eChromaMode)
    tPicFormat.eChromaMode = tStreamSettings.eChroma;

  /* Bitdepth */

  bool bUserProvidedExplicitBitdepth =
    (tPicFormat.uBitDepth != (uint8_t)OUTPUT_BD_FIRST) &&
    (tPicFormat.uBitDepth != (uint8_t)OUTPUT_BD_ALLOC) &&
    (tPicFormat.uBitDepth != (uint8_t)OUTPUT_BD_STREAM);

  if(!bUserProvidedExplicitBitdepth)
    tPicFormat.uBitDepth = tStreamSettings.iBitDepth;

  /* Plane mode */
  if(AL_PLANE_MODE_MAX_ENUM == tPicFormat.ePlaneMode)
    tPicFormat.ePlaneMode = GetInternalBufPlaneMode(tPicFormat.eChromaMode);

  if(AL_COMPONENT_ORDER_MAX_ENUM == tPicFormat.eComponentOrder)
    tPicFormat.eComponentOrder = AL_COMPONENT_ORDER_YUV;

  tUserOutputSettings.tPicFormat.eStorageMode =
      GetMainOutputStorageMode(tUserOutputSettings, tDecSettings.eFBStorageMode);

  if(IsTile(tUserOutputSettings.tPicFormat.eStorageMode))
    tUserOutputSettings.tPicFormat.eSamplePackMode = AL_SAMPLE_PACK_MODE_PACKED;

  if(tUserOutputSettings.tPicFormat.ePlaneMode == AL_PLANE_MODE_INTERLEAVED
        && tUserOutputSettings.tPicFormat.eChromaMode == AL_CHROMA_4_4_4)
    tUserOutputSettings.tPicFormat.eAlphaMode = AL_ALPHA_MODE_AFTER;
}


static string SequencePictureToString(AL_ESequenceMode sequencePicture)
{
  if(sequencePicture == AL_SM_UNKNOWN)
    return "unknown";

  if(sequencePicture == AL_SM_PROGRESSIVE)
    return "progressive";

  if(sequencePicture == AL_SM_INTERLACED)
    return "interlaced";
  return "max enum";
}

static void ShowStreamInfo(int32_t BufferNumber, int32_t BufferSize,
    AL_TStreamSettings const* pStreamSettings, AL_TCropInfo const* pCropInfo, TFourCC tFourCC,
    AL_TDimension outputDim)
{
  int32_t iWidth = outputDim.iWidth;
  int32_t iHeight = outputDim.iHeight;

  stringstream ss;
  ss << "Resolution: " << iWidth << "x" << iHeight << endl;
  ss << "FourCC: " << AL_FourCCToString(tFourCC).cFourcc << endl;
  ss << "Profile: " << AL_GET_PROFILE_IDC(pStreamSettings->eProfile) << endl;
  int32_t iOutBitdepth = AL_GetBitDepth(tFourCC);

  if(pStreamSettings->iLevel != -1)
    ss << "Level: " << pStreamSettings->iLevel << endl;
  ss << "Bitdepth: " << iOutBitdepth << endl;

  if(AL_NeedsCropping(pCropInfo))
  {
    auto uCropWidth = pCropInfo->uCropOffsetLeft + pCropInfo->uCropOffsetRight;
    auto uCropHeight = pCropInfo->uCropOffsetTop + pCropInfo->uCropOffsetBottom;
    ss << "Crop top: " << pCropInfo->uCropOffsetTop << endl;
    ss << "Crop bottom: " << pCropInfo->uCropOffsetBottom << endl;
    ss << "Crop left: " << pCropInfo->uCropOffsetLeft << endl;
    ss << "Crop right: " << pCropInfo->uCropOffsetRight << endl;
    ss << "Display resolution: " << iWidth - uCropWidth << "x" << iHeight - uCropHeight << endl;
  }
  ss << "Sequence picture: " << SequencePictureToString(pStreamSettings->eSequenceMode) << endl;
  ss << "Buffers needed: " << BufferNumber << " of size " << BufferSize << endl;

  LogInfo(CC_DARK_BLUE, "%s\n", ss.str().c_str());
}

AL_ERR DecoderContext::SetupBaseDecoderPool(int32_t iBufferNumber,
    AL_TStreamSettings const* pStreamSettings, AL_TCropInfo const* pCropInfo)
{
  auto lockDisplay = LockDisplay();

  SetDecOutputSettings(*pUserOutputSettings, *pStreamSettings, *pDecSettings);

  this->tDisplayManager.ConfigureMainOutputWriters(*pUserOutputSettings);

  if(!AL_Decoder_ConfigureOutputSettings(GetBaseDecoderHandle(), pUserOutputSettings))
    throw runtime_error("Could not configure the output settings");

  /* Compute buffer sizing */
  int32_t iBufferSize = ComputeBaseDecoderRecBufferSizing(pStreamSettings, pUserOutputSettings);

  AL_TCropInfo pUserCropInfo = *pCropInfo;

  AL_TDimension outputDim = pStreamSettings->tDim;
  ShowStreamInfo(iBufferNumber, iBufferSize, pStreamSettings, &pUserCropInfo,
      AL_GetFourCC(pUserOutputSettings->tPicFormat), outputDim);

  if(tBaseBufPool.IsInit())
    return AL_SUCCESS;

  /* Create the buffers */
  int32_t iNumBuf = iBufferNumber + uDefaultNumBuffersHeldByNextComponent;

  if(!tBaseBufPool.Init(pAllocator, iNumBuf, "decoded picture buffer"))
    return AL_ERR_NO_MEMORY;

  // Attach the metas + push to decoder
  // ----------------------------------
  for(int32_t i = 0; i < iNumBuf; ++i)
  {
    auto pDecPict = tBaseBufPool.GetSharedBuffer(AL_EBufMode::AL_BUF_MODE_NONBLOCK);

    if(!pDecPict)
      throw runtime_error("pDecPict is null");

    AL_Buffer_Cleanup(pDecPict.get());

    AttachMetaDataToBaseDecoderRecBuffer(pStreamSettings, pDecPict.get());
    bool const bAdded = AL_Decoder_PutDisplayPicture(GetBaseDecoderHandle(), pDecPict.get());

    if(!bAdded)
      throw runtime_error("bAdded must be true");
  }

  return AL_SUCCESS;
}

void DecoderContext::ReceiveBaseDecoderDecodedFrame(AL_TBuffer* pFrame)
{
  (void)pFrame;
  if(GetBaseDecoderHandle())
    iNumDecodedFrames++;
}

static void sInputParsed(AL_TBuffer* pParsedFrame, void* pUserParam, int32_t iParsingId)
{
  (void)pParsedFrame;
  (void)pUserParam;
  (void)iParsingId;
}

static void sFrameDecoded(AL_TBuffer* pFrame, void* pUserParam)
{
  auto pCtx = static_cast<DecoderContext*>(pUserParam);
  pCtx->ReceiveBaseDecoderDecodedFrame(pFrame);
}

static void sParsedSei(bool bIsPrefix, int32_t iPayloadType, uint8_t* pPayload,
                       int32_t iPayloadSize, void* pUserParam)
{
  (void)bIsPrefix;
  (void)iPayloadType;
  (void)pPayload;
  (void)iPayloadSize;
  (void)pUserParam;
}

static void sDecoderError(AL_ERR eError, void* pUserParam)
{
  auto pCtx = static_cast<DecoderContext*>(pUserParam);

  pCtx->ManageError(eError);
}

static void sBaseDecoderFrameDisplay(AL_TBuffer* pFrame, AL_TInfoDecode* pInfo, void* pUserParam)
{
  bool eos = !pFrame && !pInfo;
  bool release_only = pFrame && !pInfo;
  if(!release_only)
  {
    Ptr<Frame> frame;
    if(!eos)
    {
      frame = Frame::create(pFrame, pInfo);
      frame->invalidate();
    }
    auto pCtx = reinterpret_cast<DecoderContext*>(pUserParam);
    pCtx->ReceiveFrameToDisplayFrom(frame);
  }
}

static AL_ERR sBaseResolutionFound(int32_t iBufferNumber, AL_TStreamSettings const* pStreamSettings,
                                   AL_TCropInfo const* pCropInfo, void* pUserParam)
{
  auto pCtx = (DecoderContext*)pUserParam;
  return pCtx->SetupBaseDecoderPool(iBufferNumber, pStreamSettings, pCropInfo);
}

struct codec_error : public runtime_error
{
  explicit codec_error(AL_ERR eErrCode) : runtime_error(AL_Codec_ErrorToString(eErrCode)), Code(eErrCode)
  {
  }

  const AL_ERR Code;
};

void DecoderContext::CreateBaseDecoder(shared_ptr<I_IpDevice> device)
{
  CB.endParsingCB = { &sInputParsed, this };
  CB.endDecodingCB = { &sFrameDecoded, this };
  CB.displayCB = { &sBaseDecoderFrameDisplay, this };
  CB.resolutionFoundCB = { &sBaseResolutionFound, this };
  CB.parsedSeiCB = { &sParsedSei, this };
  CB.errorCB = { &sDecoderError, this };

  auto ctx = device->GetCtx();
  AL_ERR error = AL_Decoder_CreateWithCtx(&hBaseDec, ctx, pAllocator, pDecSettings, &CB);

  if(AL_IS_ERROR_CODE(error))
    throw codec_error(error);

  if(!hBaseDec)
    throw runtime_error("Cannot create base decoder");
}

void DecoderContext::ManageError(AL_ERR eError)
{
  if(AL_IS_ERROR_CODE(eError) || eExitCondition == DEC_WARNING)
    Rtos_SetEvent(hExitMain);
}

void DecoderContext::StartRunning(WorkerConfig wCfg)
{
  ctrlswThread = std::thread(&DecoderContext::CtrlswDecRun, this, wCfg);
  ctrlswThread.detach();
  running = true;
}

static bool IsReleaseFrame(AL_TBuffer const* pFrame, AL_TInfoDecode const* pInfo)
{
  return pFrame && !pInfo;
}

static bool IsEndOfStream(AL_TBuffer const* pFrame, AL_TInfoDecode const* pInfo)
{
  return !pFrame && !pInfo;
}

Ptr<Frame> DecoderContext::GetFrameFromQ(bool wait /*=true*/)
{
  auto timeout = (wait ? std::chrono::milliseconds(100):std::chrono::milliseconds::zero());
  Ptr<Frame> pFrame = tDisplayManager.Dequeue(timeout);
  return pFrame;
}


void DecoderContext::ReceiveFrameToDisplayFrom(Ptr<Frame> pFrame)
{
  unique_lock<mutex> lock(hDisplayMutex);

  bool bLastFrame = pFrame == nullptr;

  if(!bLastFrame)
  {
    auto err = TreatError(pFrame);

    if(AL_IS_ERROR_CODE(err))
      bLastFrame = true;
    else
    {
      {
        bool bIsFrameMainDisplay;
        auto hDec = GetDecoderHandle();
        int32_t iBitDepthAlloc = 8;

        iBitDepthAlloc = AL_Decoder_GetMaxBD(hDec);
        bool bDecoderExists = GetBaseDecoderHandle() != NULL;
        tDisplayManager.Process(pFrame, iBitDepthAlloc, bIsFrameMainDisplay, bLastFrame,
                                bDecoderExists);

        if(bIsFrameMainDisplay && CanSendBackBufferToDecoder() && !bLastFrame)
        {
          if(err == AL_WARN_CONCEAL_DETECT || err == AL_WARN_HW_CONCEAL_DETECT
               || err == AL_WARN_INVALID_ACCESS_UNIT_STRUCTURE)
            iNumFrameConceal++;

          if(!AL_Decoder_PutDisplayPicture(GetDecoderHandle(), pFrame->getBuffer()))
            throw runtime_error("bAdded must be true");
        }
      }
    }
  }

  bool bJobDone = bLastFrame;

  if(bJobDone)
    Rtos_SetEvent(hExitMain);
}

AL_ERR DecoderContext::TreatError(Ptr<Frame> frame)
{
  AL_TBuffer* pFrame = frame->getBuffer();
  bool bExitError = false;
  AL_ERR err = AL_SUCCESS;

  auto hDec = GetDecoderHandle();

  if(hDec)
  {
    err = AL_Decoder_GetFrameError(hDec, pFrame);
    bExitError |= AL_IS_ERROR_CODE(err);
  }

  if(bExitError)
  {
    LogDimmedWarning("\n%s\n", AL_Codec_ErrorToString(err));

    if(err == AL_WARN_SEI_OVERFLOW)
      LogDimmedWarning("\nDecoder has discarded some SEI while the SEI metadata buffer was too small\n");

    LogError("Error: %d\n", err);
  }

  return err;
}

/****************************************************************************/
/*******Class AsyncFileInput*************************************************/
/****************************************************************************/
AsyncFileInput::AsyncFileInput() {}

AsyncFileInput::~AsyncFileInput(void)
{
  m_bExit = true;

  if(m_thread.joinable())
    m_thread.join();
}

void AsyncFileInput::Init(AL_HDecoder hDec, BufPool& bufPool, EndOfInputCallBack endOfInputCB,
                          PushBufferCallBack pushBufferCB)
{
  m_hDec = hDec;
  m_pBufPool = &bufPool;
  m_pushBufferCB = pushBufferCB;
  m_endOfInputCB = endOfInputCB;
  m_bExit = false;
}

void AsyncFileInput::ConfigureStreamInput(string const& sPath, AL_ECodec eCodec)
{
  (void)eCodec;
  try {
    OpenInput(m_ifFileStream, sPath);
    m_bStreamInputSet = true;
    m_StreamLoader.reset(new BasicLoader());
  }
  catch (const std::exception& e) {
    CV_Error(cv::Error::StsBadArg, "Failed to open input stream:");
  }
}

void AsyncFileInput::Start(void)
{
  if(!m_bStreamInputSet)
    throw runtime_error("Stream input must be set (call AsyncFileInput::ConfigureStreamInput)");

  m_thread = thread(&AsyncFileInput::Run, this);
}

void AsyncFileInput::Run(void)
{
  Rtos_SetCurrentThreadName("FileInput");

  while(!m_bExit)
  {
    shared_ptr<AL_TBuffer> pInputBuf;
    try
    {
      pInputBuf = m_pBufPool->GetSharedBuffer();
    }
    catch(bufpool_decommited_error &)
    {
      continue;
    }

    uint8_t uBufFlags;
    bool bInputFinished = false;
    uint32_t uAvailSize = 0;

    uAvailSize = m_StreamLoader->ReadStream(m_ifFileStream, pInputBuf.get(), uBufFlags);
    bInputFinished = !uAvailSize;

    if(bInputFinished)
    {
      m_endOfInputCB(m_hDec);
      break;
    }

    auto bRet = m_pushBufferCB(m_hDec, pInputBuf.get(), uAvailSize, uBufFlags);

    if(!bRet)
      throw runtime_error("Failed to push buffer");
  }
}

void ShowStatistics(double durationInSeconds, int32_t iNumFrameConceal, int32_t decodedFrameNumber,
                    bool timeoutOccurred)
{
  string guard = "Decoded time = ";

  if(timeoutOccurred)
    guard = "TIMEOUT = ";

  auto msg = guard + "%.4f s;  Decoding FrameRate ~ %.4f Fps; Frame(s) conceal = %d\n";
  LogInfo(msg.c_str(),
          durationInSeconds,
          decodedFrameNumber / durationInSeconds,
          iNumFrameConceal);
}

void AdjustStreamBufferSettings(Config& config)
{
  uint32_t uMinStreamBuf = config.tDecSettings.iStackSize;
  config.uInputBufferNum = max(uMinStreamBuf, config.uInputBufferNum);
  config.zInputBufferSize = max(size_t(1), config.zInputBufferSize);
}

void CheckAndAdjustChannelConfiguration(Config& config)
{
  FILE* out = g_Verbosity ? stdout : nullptr;

  // Check base decoder settings
  // ---------------------------
  {
    int32_t err = AL_DecSettings_CheckValidity(&config.tDecSettings, out);
    err += AL_DecOutputSettings_CheckValidity(&config.tUserOutputSettings,
                                              config.tDecSettings.eCodec, out);

    if(err)
    {
      stringstream ss;
      ss << err << " errors(s). " << "Invalid settings, please check the parameters.";
      CV_Error(cv::Error::StsBadArg, ss.str());
    }

    auto const incoherencies = AL_DecSettings_CheckCoherency(&config.tDecSettings, out);

    if(incoherencies < 0)
        CV_Error(cv::Error::StsBadArg,
            "Fatal coherency error in settings, please check the parameters.");
  }

  // Adjust settings
  // ---------------
  AdjustStreamBufferSettings(config);
}

void ConfigureInputPool(Config const& config, AL_TAllocator* pAllocator, BufPool& tInputPool)
{
  std::string sDebugName = "input_pool";
  uint32_t uNumBuf = config.uInputBufferNum;
  uint32_t zBufSize = config.zInputBufferSize;
  AL_TMetaData* pBufMeta = nullptr;

  auto ret = tInputPool.Init(pAllocator, uNumBuf, zBufSize, pBufMeta, sDebugName);

  if(pBufMeta != nullptr)
    AL_MetaData_Destroy(pBufMeta);

  if(!ret)
    throw runtime_error("Can't create BufPool");
}


/****************************************************************************/
/*******Ctrlsw Open Create Run **********************************************/
/****************************************************************************/
void DecoderContext::CtrlswDecRun(WorkerConfig wCfg)
{
  auto& config = *wCfg.pConfig;
  AL_TAllocator* pAllocator = nullptr;

  pAllocator = wCfg.device->GetAllocator();

  // Configure the stream buffer pool
  // --------------------------------
  // Note : Must be before scopeExit so that AL_Decoder_Destroy can be called
  // before the BufPool destroyer. Can it be done differently so that it is not dependant of this order ?
  BufPool tInputPool;
  ConfigureInputPool(config, pAllocator, tInputPool);

  // Insure destroying is done even after throwing
  // ---------------------------------------------
  auto scopeDecoder = scopeExit([&]() {
    StopSendingBuffer(); // Prevent to push buffer to the decoder while destroying it
    AL_Decoder_Destroy(GetBaseDecoderHandle());
  });

  // Start feeding the decoder
  // -------------------------
  auto const uBegin = GetPerfTime();
  bool timeoutOccurred = false;

  {
    tInputPool.Commit();

    // Setup the reader of bitstream in the file.
    // It will send bitstream chunk to the decoder
    AsyncFileInput producer;
    AL_ECodec eCodec = config.tDecSettings.eCodec;

    producer.Init(GetBaseDecoderHandle(), tInputPool, AL_Decoder_Flush, AL_Decoder_PushStreamBuffer);
    producer.ConfigureStreamInput(config.sIn, eCodec);
    producer.Start();

    auto const maxWait = config.iTimeoutInSeconds * 1000;
    auto const timeout = maxWait >= 0 ? maxWait : AL_WAIT_FOREVER;

    if(!WaitExit(timeout))
      timeoutOccurred = true;

    tInputPool.Decommit();
  }

  auto const uEnd = GetPerfTime();

  // Prevent the display to produce some outputs
  auto lock = LockDisplay();

  // Get the errors
  // --------------
  AL_ERR eErr = AL_SUCCESS;

  if(GetBaseDecoderHandle())
    eErr = AL_Decoder_GetLastError(GetBaseDecoderHandle());

  if(AL_IS_ERROR_CODE(eErr) || (AL_IS_WARNING_CODE(eErr) && config.eExitCondition == DEC_WARNING))
  {
    throw codec_error(eErr);
  }

  if(AL_IS_WARNING_CODE(eErr))
    cerr << endl << "Warning: " << AL_Codec_ErrorToString(eErr) << endl;

  if(!GetNumDecodedFrames())
    throw runtime_error("No frame decoded");

  auto const duration = (uEnd - uBegin) / 1000.0;
  ShowStatistics(duration, GetNumConcealedFrame(), GetNumDecodedFrames(), timeoutOccurred);
  eos = true;
}

static std::shared_ptr<DecCIpDevice> CreateAndConfigureBaseDecoderIpDevice(Config const* pConfig)
{
  DecCIpDeviceParam param;

  param.eSchedulerType = pConfig->eSchedulerType;
  param.eDeviceType = pConfig->eDeviceType;
  param.bTrackDma = pConfig->trackDma;
  param.uNumCore = pConfig->tDecSettings.uNumCore;
  param.iHangers = pConfig->hangers;
  param.ipCtrlMode = pConfig->ipCtrlMode;
  param.apbFile = pConfig->apbFile;
  static std::set<std::string> decDevicePath = pConfig->sDecDevicePath;
  param.iDecMaxAxiBurstSize = pConfig->iDecMaxAxiBurstSize;

  std::shared_ptr<DecCIpDevice> pIpDevice = std::shared_ptr<DecCIpDevice>(
      new DecCIpDevice(param, pConfig->eDeviceType, { decDevicePath }));

  if(!pIpDevice)
    throw std::runtime_error("Can't create BaseDecoderIpDevice");

  return pIpDevice;
}

void CtrlswDecOpen(std::shared_ptr<Config> pDecConfig,
                   std::shared_ptr<DecoderContext>& pDecodeCtx, WorkerConfig& wCfg)
{
  std::set<std::string> const sDecDefaultDevicePath(DECODER_DEVICES);
  SetDefaultDecOutputSettings(&pDecConfig->tUserOutputSettings);
  pDecConfig->sDecDevicePath = sDecDefaultDevicePath;

  // Setup of the decoder(s) architecture
  AL_Lib_Decoder_Init(AL_LIB_DECODER_ARCH_RISCV);

  // Create the device
  std::shared_ptr<I_IpDevice> device;
  try
  {
      device = CreateAndConfigureBaseDecoderIpDevice(pDecConfig.get());
  } catch (const std::exception& e) {
      CV_Error(cv::Error::StsError, e.what());
  }

  auto& config = *pDecConfig;
  AL_TAllocator* pAllocator = nullptr;

  pAllocator = device->GetAllocator();

  // Settings checks
  // ------------------
  CheckAndAdjustChannelConfiguration(config);

  // Configure the decoders
  // ----------------------
  pDecodeCtx = std::shared_ptr<DecoderContext>(new DecoderContext(config, pAllocator));

  wCfg.pConfig = pDecConfig;
  wCfg.device = device;

  // Create the decoders
  // -------------------
  pDecodeCtx->CreateBaseDecoder(device);

  // Parametrization of the base decoder for traces
  // ----------------------------------------------
  auto hDec = pDecodeCtx->GetBaseDecoderHandle();
  AL_Decoder_SetParam(hDec, "Fpga", config.iTraceIdx, config.iTraceNumber,
      config.ipCtrlMode == AL_EIpCtrlMode::AL_IPCTRL_MODE_TRACE);

  // Parametrization of the lcevc decoder for traces
  // -----------------------------------------------
}

} } // namespace cv::vcucodec
