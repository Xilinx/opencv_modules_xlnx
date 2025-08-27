
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
#ifndef OPENCV_VCUCODEC_VCUDECCONTEXT_HPP
#define OPENCV_VCUCODEC_VCUDECCONTEXT_HPP

#include <opencv2/core.hpp>

#include "vcurawout.hpp"
#include "vcudevice.hpp"

extern "C" {
#include "lib_common/FourCC.h"
#include "lib_common_dec/DecOutputSettings.h"
#include "lib_decode/DecSettings.h"

}

#include "lib_app/utils.h"

#include <cstdint>
#include <memory>
#include <set>
#include <string>

namespace cv {
namespace vcucodec {


class DecContext
{
  public:

    class Config;
    class WorkerConfig;

    virtual ~DecContext() = default;

    /// Start the decoder context with the given worker configuration.
    virtual void start(WorkerConfig wCfg) = 0;

    /// Wait for the decoder context to finish processing.
    virtual void finish() = 0;

    /// Check if the decoder context is running.
    virtual bool running() const = 0;

    /// Check if the end of stream has been reached.
    virtual bool eos() const = 0;

    /// Return information on the stream, available once the headers are parsed.
    virtual String streamInfo() const = 0;

    // Return statistics on the decoding process, available once decoding has finished.
    virtual String statistics() const = 0;

    static std::shared_ptr<DecContext> create(std::shared_ptr<Config> pDecConfig,
        Ptr<RawOutput> rawOutput, WorkerConfig& wCfg);
};


enum EDecErrorLevel
{
  DEC_WARNING,
  DEC_ERROR,
};

enum OutputBitDepth {
    OUTPUT_BD_FIRST = 0,
    OUTPUT_BD_ALLOC = -1,
    OUTPUT_BD_STREAM = -2
};

struct DecContext::Config
{
  Config();
  static int32_t const zDefaultInputBufferSize = 32 * 1024;
  bool help = false;

  std::string sIn;
  std::string sMainOut = "default.yuv"; // Output rec file
  std::string sCrc;

  AL_TDecSettings tDecSettings {};
  AL_TDecOutputSettings tUserOutputSettings {};
  bool bEnableCrop = false;
  int32_t iDecMaxAxiBurstSize = 0;

  #ifdef HAVE_VCU2_CTRLSW
  AL_EDeviceType eDeviceType = AL_EDeviceType::AL_DEVICE_TYPE_EMBEDDED;
  #elif defined(HAVE_VCU_CTRLSW)
  AL_EDeviceType eDeviceType = AL_EDeviceType::AL_DEVICE_TYPE_BOARD;
  #endif
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
  bool enableByRef = false;
  EDecErrorLevel eExitCondition = DEC_ERROR;
  uint32_t uNumBuffersHeldByNextComponent = 1;
};

struct DecContext::WorkerConfig
{
  std::shared_ptr<Config> pConfig;
  Ptr<Device> device;
};

using Config = DecContext::Config;
using WorkerConfig = DecContext::WorkerConfig;


} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUDECCONTEXT_HPP
