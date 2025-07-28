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

#ifndef OPENCV_CTRLSW_DEC_HPP
#define OPENCV_CTRLSW_DEC_HPP

#include <opencv2/core.hpp>

#include <iostream>
#include <string.h>
#include <functional>
#include <string>
#include <set>
#include <thread>
#include <queue>
#include <deque>
#include <cstddef>
#include <map>
#include <condition_variable>
#include <sstream>

#include <atomic>

extern "C" {
#include "config.h"
#include "lib_decode/lib_decode.h"
#include "lib_common/FourCC.h"
#include "lib_decode/DecSettings.h"
#include "lib_common/PicFormat.h"
#include "lib_common_dec/DecOutputSettings.h"
#include "lib_common_dec/DecoderTraceHook.h"
#include "lib_common/Context.h"
#include "lib_fpga/DmaAlloc.h"
#include "lib_log/LoggerDefault.h"
#include "lib_decode/LibDecoderRiscv.h"
#include "lib_common/BufCommon.h"
#include "lib_common/BufferAPI.h"
#include "lib_common/BufferHandleMeta.h"
#include "lib_common/BufferSeiMeta.h"
#include "lib_common/DisplayInfoMeta.h"
#include "lib_common/Error.h"
#include "lib_common/PixMapBuffer.h"
#include "lib_common/StreamBuffer.h"
#include "lib_common_dec/DecBuffers.h"
#include "lib_common_dec/IpDecFourCC.h"
#include "lib_common_dec/HDRMeta.h"
#include "lib_common/FbcMapSize.h"
#include "lib_common/BufferPictureDecMeta.h"
}

#include "lib_app/PixMapBufPool.h"
#include "lib_app/SinkFilter.h"
#include "lib_app/SinkCrop.h"
#include "lib_app/SinkCrcDump.h"
 #include "lib_app/SinkFrame.h"
#include "lib_app/UnCompFrameReader.h"
#include "lib_app/YuvIO.h"
#include "lib_app/console.h"
#include "lib_app/convert.h"
#include "lib_app/plateform.h"
#include "lib_app/timing.h"
#include "lib_app/utils.h"
#include "lib_app/BufPool.h"
#include <cassert>

namespace cv {
namespace vcucodec {

using namespace std;

/*****************************************************************************/
typedef struct AL_TAllocator AL_TAllocator;
typedef struct AL_TIpCtrl AL_TIpCtrl;
typedef struct AL_TDriver AL_TDriver;

/*****************************************************************************/
/*******class DecCIpDevice ***************************************************/
/*****************************************************************************/
struct DecCIpDeviceParam
{
  AL_EDeviceType eDeviceType;
  AL_ESchedulerType eSchedulerType;
  bool bTrackDma = false;
  uint8_t uNumCore = 0;
  int32_t iHangers = 0;
  AL_EIpCtrlMode ipCtrlMode;
  std::string apbFile;
  int32_t iDecMaxAxiBurstSize = 0;
};

struct I_IpDevice
{
  virtual ~I_IpDevice() = default;
  virtual void* GetScheduler() = 0;
  virtual AL_RiscV_Ctx GetCtx() = 0;
  virtual AL_TAllocator* GetAllocator() = 0;
  virtual AL_ITimer* GetTimer() = 0;
};

typedef struct AL_IDecScheduler AL_IDecScheduler;

class DecCIpDevice : public I_IpDevice
{
public:
  DecCIpDevice(DecCIpDeviceParam const& param, AL_EDeviceType eDeviceType,
               std::set<std::string> tDevices);
  ~DecCIpDevice();

  AL_EDeviceType GetDeviceType();
  void* GetScheduler() override;
  AL_RiscV_Ctx GetCtx() override;
  AL_TAllocator* GetAllocator() override;
  AL_ITimer* GetTimer() override;

  DecCIpDevice(DecCIpDevice const &) = delete;
  DecCIpDevice & operator = (DecCIpDevice const &) = delete;

private:
  std::set<std::string> const m_tDevices;
  std::string m_tSelectedDevice;
  AL_EDeviceType m_eDeviceType;
  AL_IDecScheduler* m_pScheduler = nullptr;
  std::shared_ptr<AL_TAllocator> m_pAllocator = nullptr;
  AL_RiscV_Ctx m_ctx = nullptr;
  AL_ITimer* m_pTimer = nullptr;

  void ConfigureRiscv();
};

inline void* DecCIpDevice::GetScheduler(void)
{
  return m_pScheduler;
}

inline AL_RiscV_Ctx DecCIpDevice::GetCtx(void)
{
  return m_ctx;
}

inline AL_TAllocator* DecCIpDevice::GetAllocator(void)
{
  return m_pAllocator.get();
}

inline AL_ITimer* DecCIpDevice::GetTimer(void)
{
  return m_pTimer;
}

/*****************************************************************************/
/*******class Config *********************************************************/
/*****************************************************************************/
enum EDecErrorLevel
{
  DEC_WARNING,
  DEC_ERROR,
};

/******************************************************************************/
static int32_t const zDefaultInputBufferSize = 32 * 1024;
static const int32_t OUTPUT_BD_FIRST = 0;
static const int32_t OUTPUT_BD_ALLOC = -1;
static const int32_t OUTPUT_BD_STREAM = -2;
static const int32_t SEI_NOT_ASSOCIATED_WITH_FRAME = -1;
static uint32_t constexpr uDefaultNumBuffersHeldByNextComponent = 1; /* We need at least 1 buffer to copy the output on a file */
static const int32_t DEFAULT_DEC_APB_ID = 16;

struct Config
{
  Config();

  bool help = false;

  std::string sIn;
  std::string sMainOut = "default.yuv"; // Output rec file
  std::string sCrc;

  AL_TDecSettings tDecSettings {};
  AL_TDecOutputSettings tUserOutputSettings {};
  bool bEnableCrop = false;
  int32_t iDecMaxAxiBurstSize = 0;

  AL_EDeviceType eDeviceType = AL_EDeviceType::AL_DEVICE_TYPE_EMBEDDED;
  AL_ESchedulerType eSchedulerType = AL_ESchedulerType::AL_SCHEDULER_TYPE_CPU;
  int32_t iOutputBitDepth = OUTPUT_BD_ALLOC;
  TFourCC tOutputFourCC = FOURCC(NULL);
  int32_t iTraceIdx = -1;
  int32_t iTraceNumber = 0;
  bool bForceCleanBuffers = false;
  bool bEnableYUVOutput = true;
  uint32_t uInputBufferNum = 2;
  size_t zInputBufferSize = zDefaultInputBufferSize;
  AL_EIpCtrlMode ipCtrlMode = AL_EIpCtrlMode::AL_IPCTRL_MODE_STANDARD;
  std::string md5File = "";
  std::string apbFile = "";
  std::string sSplitSizesFile = "";
  bool trackDma = false;
  int32_t hangers = 0;
  int32_t iLoop = 1;
  bool bCertCRC = false;
  std::set<std::string> sDecDevicePath;
  int32_t iTimeoutInSeconds = -1;
  int32_t iMaxFrames = INT32_MAX;
  bool bUsePreAlloc = false;

  bool UseBaseDecoder() const { return true; }

  EDecErrorLevel eExitCondition = DEC_ERROR;
};


class Frame
{
  Frame(AL_TBuffer* pFrame, AL_TInfoDecode const* pInfo)
    : pFrame_(pFrame), info_(*pInfo)
  {
      AL_Buffer_Ref(pFrame_);
      AL_Buffer_InvalidateMemory(pFrame_);
  }

  Frame(Frame const& frame) // shallow copy constructor
  {
    pFrame_ = AL_Buffer_ShallowCopy(frame.pFrame_, &sFreeWithoutDestroyingMemory);
    AL_Buffer_Ref(pFrame_);
    AL_TMetaData* pMetaD;
    AL_TPixMapMetaData* pPixMeta = (AL_TPixMapMetaData*)AL_Buffer_GetMetaData(
        frame.pFrame_, AL_META_TYPE_PIXMAP);
    if (!pPixMeta)
      throw runtime_error("PixMapMetaData is NULL");
    AL_TDisplayInfoMetaData* pDispMeta = (AL_TDisplayInfoMetaData*)AL_Buffer_GetMetaData(
        frame.pFrame_, AL_META_TYPE_DISPLAY_INFO);
    if (!pDispMeta)
      throw runtime_error("PixMapMetaData is NULL");
    pMetaD = (AL_TMetaData*)AL_PixMapMetaData_Clone(pPixMeta);
    if (!pMetaD)
      throw runtime_error("Clone of PixMapMetaData was not created!");
    AL_Buffer_AddMetaData(pFrame_, pMetaD);
    pMetaD = (AL_TMetaData*)AL_DisplayInfoMetaData_Clone(pDispMeta);
    if (!pMetaD)
      throw runtime_error("Clone of PixMapMetaData was not created!");
    if (!AL_Buffer_AddMetaData(pFrame_, pMetaD))
      throw runtime_error("Cloned pMetaD did not get added!\n");

    info_ = frame.info_;
  }

public:

  ~Frame() {
   if (pFrame_) {
      AL_Buffer_Unref(pFrame_);
      pFrame_ = nullptr;
    }
  }

  AL_TBuffer* getBuffer() const { return pFrame_; }
  AL_TInfoDecode const & getInfo() const { return info_; }
  bool isMainOutput() const {
    return (info_.eOutputID == AL_OUTPUT_MAIN || info_.eOutputID == AL_OUTPUT_POSTPROC);
  }
  unsigned int bitDepthY() const {
    return info_.uBitDepthY;
  }
  unsigned int bitDepthUV() const {
    return info_.uBitDepthC;
  }

  AL_TCropInfo const& getCropInfo() const {
    return info_.tCrop;
  }

  AL_TDimension const& getDimension() const {
    return info_.tDim;
  }

  static Ptr<Frame> create(AL_TBuffer* pFrame, AL_TInfoDecode const* pInfo)
  {
    return Ptr<Frame>(new Frame(pFrame, pInfo));
  }

  static Ptr<Frame> createShallowCopy(Ptr<Frame> const& frame)
  {
    if (!frame || !frame->getBuffer()) {
      return Ptr<Frame>();
    }
    return Ptr<Frame>(new Frame(*frame));
  }

private:
  static void sFreeWithoutDestroyingMemory(AL_TBuffer* buffer)
  {
    buffer->iChunkCnt = 0;
    AL_Buffer_Destroy(buffer);
  }
  AL_TBuffer* pFrame_ = nullptr;
  AL_TInfoDecode info_;
};

/******************************************************************************/
/*******class DisplayManager **************************************************/
/******************************************************************************/

class DisplayManager
{
public:
  void Configure(Config const& config);
  void ConfigureMainOutputWriters(AL_TDecOutputSettings const& tDecOutputSettings);

  bool Process(Ptr<Frame> frame, int32_t iBitDepthAlloc,
               bool& bIsMainDisplay, bool& bNumFrameReached, bool bDecoderExists);
  void Enqueue(AL_TBuffer* pFrame);
  AL_TBuffer* Dequeue(std::chrono::milliseconds timeout);

private:
  void ProcessFrame(Ptr<Frame>, int32_t iBdOut, TFourCC tOutFourCC);

  void CopyMetaData(AL_TBuffer* pDstFrame, AL_TBuffer* pSrcFrame, AL_EMetaType eMetaType);

  //unique_ptr<MultiSink> multisinkRaw = unique_ptr<MultiSink>(new MultiSink);
  unique_ptr<MultiSink> multisinkOut = unique_ptr<MultiSink>(new MultiSink);

  AL_EFbStorageMode eMainOutputStorageMode;
  bool bOutputWritersCreated = false;
  int32_t iBitDepth = 8;
  uint32_t uNumFrames = 0;
  uint32_t uMaxFrames = UINT32_MAX;
  TFourCC tOutputFourCC = FOURCC(NULL);
  TFourCC tInputFourCC = FOURCC(NULL);

  bool bHasOutput = false;
  bool bEnableYuvOutput = false;
  std::shared_ptr<ofstream> hFileOut;
  std::shared_ptr<ofstream> hMapOut;
  std::queue<AL_TBuffer*> frame_queue;
  std::mutex frame_mtx;
  std::condition_variable frame_cv;
};

/******************************************************************************/
/*******class DecoderContext **************************************************/
/******************************************************************************/

struct WorkerConfig
{
  std::shared_ptr<Config> pConfig;
  std::shared_ptr<I_IpDevice> device;
};

class DecoderContext
{
public:
  DecoderContext(Config& config, AL_TAllocator* pAllocator);
  ~DecoderContext();
  void CreateBaseDecoder(shared_ptr<I_IpDevice> device);
  AL_HDecoder GetBaseDecoderHandle() const { return hBaseDec; }
  AL_ERR SetupBaseDecoderPool(int32_t iBufferNumber, AL_TStreamSettings const* pStreamSettings,
                              AL_TCropInfo const* pCropInfo);

  bool WaitExit(uint32_t uTimeout);
  void ReceiveFrameToDisplayFrom(Ptr<Frame> pFrame);
  int32_t GetNumConcealedFrame() const { return iNumFrameConceal; };
  int32_t GetNumDecodedFrames() const { return iNumDecodedFrames; };
  std::unique_lock<mutex> LockDisplay() { return std::unique_lock<mutex>(hDisplayMutex); };
  void StopSendingBuffer() { LockDisplay(); bPushBackToDecoder = false; };
  bool CanSendBackBufferToDecoder() { return bPushBackToDecoder; };
  void ReceiveBaseDecoderDecodedFrame(AL_TBuffer* pFrame);
  void ManageError(AL_ERR eError);
  AL_TBuffer* GetFrameFromQ(bool wait = true);
  void StartRunning(WorkerConfig wCfg);
  bool running;
  bool eos;

private:
  AL_TAllocator* pAllocator;
  AL_HDecoder hBaseDec = nullptr;
  DisplayManager tDisplayManager {};
  bool bPushBackToDecoder = true;
  int32_t iNumFrameConceal = 0;
  int32_t iNumDecodedFrames = 0;
  AL_TDecCallBacks CB {};
  AL_TDecSettings* pDecSettings;
  bool bUsePreAlloc = false;
  PixMapBufPool tBaseBufPool;
  AL_TDecOutputSettings* pUserOutputSettings;
  ofstream seiOutput;
  ofstream seiSyncOutput;
  std::thread ctrlswThread;

  AL_HANDLE GetDecoderHandle() const;
  AL_ERR TreatError(Ptr<Frame> pFrame);
  AL_TDimension ComputeBaseDecoderFinalResolution(AL_TStreamSettings const* pStreamSettings);
  int32_t ComputeBaseDecoderRecBufferSizing(AL_TStreamSettings const* pStreamSettings,
                                            AL_TDecOutputSettings const* pUserOutputSettings);
  void AttachMetaDataToBaseDecoderRecBuffer(AL_TStreamSettings const* pStreamSettings,
                                            AL_TBuffer* pDecPict);
  void CtrlswDecRun(WorkerConfig wCfg);

  std::map<AL_TBuffer*, std::vector<AL_TSeiMetaData*>> displaySeis;
  EDecErrorLevel eExitCondition = DEC_ERROR;
  AL_EVENT hExitMain = nullptr;
  mutex hDisplayMutex;
};

/****************************************************************************/
/*******Class AsyncFileInput*************************************************/
/****************************************************************************/
struct InputLoader
{
  virtual ~InputLoader() {}
  virtual uint32_t ReadStream(std::istream& ifFileStream, AL_TBuffer* pBufStream,
                              uint8_t& uBufFlags) = 0;
};

struct BasicLoader : public InputLoader
{
  uint32_t ReadStream(std::istream& ifFileStream, AL_TBuffer* pBufStream, uint8_t& uBufFlags) override
  {
    uint8_t* pBuf = AL_Buffer_GetData(pBufStream);

    ifFileStream.read((char*)pBuf, AL_Buffer_GetSize(pBufStream));

    uBufFlags = AL_STREAM_BUF_FLAG_UNKNOWN;

    return (uint32_t)ifFileStream.gcount();
  }
};

typedef void (* EndOfInputCallBack)(AL_HANDLE hDec);
typedef bool (* PushBufferCallBack)(AL_HANDLE hDec, AL_TBuffer* pBuf, size_t uSize, uint8_t uFlags);

struct AsyncFileInput
{
  AsyncFileInput();
  ~AsyncFileInput();
  void Init(AL_HDecoder hDec_, BufPool& bufPool_, EndOfInputCallBack endOfInputCB_,
            PushBufferCallBack pushBufferCB_);
  void ConfigureStreamInput(string const& sPath, AL_ECodec eCodec);
  void Start();

private:
  void Run();

  AL_HDecoder m_hDec;
  ifstream m_ifFileStream;
  ifstream ifFileSizes;
  BufPool* m_pBufPool;
  bool m_bStreamInputSet = false;
  std::unique_ptr<InputLoader> m_StreamLoader;
  thread m_thread;
  PushBufferCallBack m_pushBufferCB;
  EndOfInputCallBack m_endOfInputCB;
  atomic<bool> m_bExit;

};

void CtrlswDecOpen(std::shared_ptr<Config> pDecConfig, std::shared_ptr<DecoderContext>& pDecodeCtx,
                   WorkerConfig& wCfg);

} } // namespace cv::vcucodec

#endif // OPENCV_CTRLSW_DEC_HPP