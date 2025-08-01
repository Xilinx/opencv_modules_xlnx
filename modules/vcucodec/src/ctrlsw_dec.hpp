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

#include "private/vcuframe.hpp"
#include "private/vcurawout.hpp"
#include "private/vcudevice.hpp"
namespace cv {
namespace vcucodec {

using namespace std;

/*****************************************************************************/
typedef struct AL_TAllocator AL_TAllocator;
typedef struct AL_TIpCtrl AL_TIpCtrl;
typedef struct AL_TDriver AL_TDriver;
typedef struct AL_IDecScheduler AL_IDecScheduler;

/*****************************************************************************/
/*******class Config *********************************************************/
/*****************************************************************************/
enum EDecErrorLevel
{
  DEC_WARNING,
  DEC_ERROR,
};

enum OutputBd{
    OUTPUT_BD_FIRST = 0,
    OUTPUT_BD_ALLOC = -1,
    OUTPUT_BD_STREAM = -2
};

/******************************************************************************/
static int32_t const zDefaultInputBufferSize = 32 * 1024;

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

/******************************************************************************/
/*******class DecoderContext **************************************************/
/******************************************************************************/

struct WorkerConfig
{
  std::shared_ptr<Config> pConfig;
  Ptr<Device> device;
};
class DecContext
{
  public:
    virtual ~DecContext() = default;

    /// Start the decoder context with the given worker configuration.
    virtual void start(WorkerConfig wCfg) = 0;

    /// Wait for the decoder context to finish processing.
    virtual void finish() = 0;

    /// Check if the decoder context is running.
    virtual bool running() const = 0;

    /// Check if the end of stream has been reached.
    virtual bool eos() const = 0;

    static std::shared_ptr<DecContext> create(std::shared_ptr<Config> pDecConfig,
        Ptr<RawOutput> rawOutput, WorkerConfig& wCfg);
};

class DecoderContext : public DecContext
{
public:
  DecoderContext(Config& config, AL_TAllocator* pAllocator, Ptr<RawOutput> rawOutput);
  ~DecoderContext();
  void CreateBaseDecoder(Ptr<Device> device);
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
  void FrameDone(Frame const& f);
  void ManageError(AL_ERR eError);
  Ptr<Frame> GetFrameFromQ(bool wait = true);
  void start(WorkerConfig wCfg) override;
  void finish() override;

  bool running() const override { return running_; }
  bool eos() const override { return eos_; }

  private:
  bool running_;
  bool eos_;
  bool await_eos_;

  AL_TAllocator* pAllocator;
  AL_HDecoder hBaseDec = nullptr;
  Ptr<RawOutput> tDisplayManager {};
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


} // namespace vcucodec
} // namespace cv

#endif // OPENCV_CTRLSW_DEC_HPP