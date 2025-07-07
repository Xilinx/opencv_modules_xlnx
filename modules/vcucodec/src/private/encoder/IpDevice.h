// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "CfgParser.h"
#include "lib_app/utils.h"

#include <string>

extern "C"
{
#include "lib_encode/lib_encoder.h"
#include "lib_log/LoggerInterface.h"
#include "lib_log/TimerInterface.h"
}

typedef struct AL_TAllocator AL_TAllocator;
typedef struct AL_TIpCtrl AL_TIpCtrl;

/*****************************************************************************/
struct CIpDeviceParam
{
  AL_EDeviceType eDeviceType;
  AL_ESchedulerType eSchedulerType;
  ConfigFile* pCfgFile;
  bool bTrackDma = false;
};

/*****************************************************************************/
static int32_t constexpr NUM_SRC_SYNC_CHANNEL = 4;

class CIpDevice
{
public:
  CIpDevice() = default;
  ~CIpDevice();

  void Configure(CIpDeviceParam& param);
  AL_IEncScheduler* GetScheduler();
  AL_RiscV_Ctx GetCtx();
  AL_TAllocator* GetAllocator();
  AL_ITimer* GetTimer();

  CIpDevice(CIpDevice const &) = delete;
  CIpDevice & operator = (CIpDevice const &) = delete;

private:
  AL_IEncScheduler* m_pScheduler = nullptr;
  std::shared_ptr<AL_TAllocator> m_pAllocator = nullptr;
  AL_RiscV_Ctx m_ctx = nullptr;
  AL_ITimer* m_pTimer = nullptr;

  void ConfigureRiscv(CIpDeviceParam& param);

};

inline AL_IEncScheduler* CIpDevice::GetScheduler(void)
{
  return m_pScheduler;
}

inline AL_RiscV_Ctx CIpDevice::GetCtx(void)
{
  return m_ctx;
}

inline AL_TAllocator* CIpDevice::GetAllocator(void)
{
  return m_pAllocator.get();
}

inline AL_ITimer* CIpDevice::GetTimer(void)
{
  return m_pTimer;
}



#include <stdexcept>
#include "lib_app/AllocatorHelper.h"
#include <algorithm>
#include <sstream>

extern "C"
{
#include "lib_fpga/DmaAlloc.h"
#include "lib_log/TimerSoftware.h"
#include "lib_rtos/utils.h"
}

using namespace std;

extern "C"
{
#include "lib_encode/LibEncoderRiscv.h"
}

inline void CIpDevice::ConfigureRiscv(CIpDeviceParam& param)
{
  int32_t iEncDevicePathscount;
  uint32_t const sw_version = (uint32_t)((AL_VERSION_MAJOR << 20) + (AL_VERSION_MINOR << 12) + (AL_VERSION_PATCH));
  uint32_t fw_version;

  m_pScheduler = nullptr;
  m_ctx = nullptr;

  iEncDevicePathscount = param.pCfgFile->RunInfo.encDevicePaths.size();

  for(int32_t i = (iEncDevicePathscount - 1); i >= 0; --i)
  {
    const char* sDevicePath = param.pCfgFile->RunInfo.encDevicePaths[i].c_str();
    m_ctx = AL_Riscv_Encode_CreateCtx(sDevicePath);

    if(m_ctx)
      break;
    Rtos_Log(AL_LOG_INFO, "Failed to open device '%s'", sDevicePath);
  }

  if(!m_ctx)
    throw std::runtime_error("Failed to create context");

  fw_version = AL_Riscv_Encode_Get_FwVersion(m_ctx);

  if(!fw_version || (fw_version != sw_version))
    throw runtime_error("FW Version " + VersionToStr(fw_version) + ", it should be " + VersionToStr(sw_version));

  AL_TAllocator* pRiscvAllocator = AL_Riscv_Encode_DmaAlloc_Create(m_ctx);
  m_pTimer = nullptr;

  if(!pRiscvAllocator)
    throw runtime_error("Can't find dma allocator");

  m_pAllocator.reset(pRiscvAllocator, &AL_Allocator_Destroy);
}

inline CIpDevice::~CIpDevice(void)
{
  if(m_pScheduler)
    AL_IEncScheduler_Destroy(m_pScheduler);

  if(m_pTimer)
    AL_ITimer_Deinit(m_pTimer);

  if(m_ctx)
    AL_Riscv_Encode_DestroyCtx(m_ctx);

}

inline void CIpDevice::Configure(CIpDeviceParam& param)
{

  if(param.eDeviceType == AL_EDeviceType::AL_DEVICE_TYPE_EMBEDDED)
  {
    ConfigureRiscv(param);
    return;
  }

  throw runtime_error("No support for this scheduling type");
}

