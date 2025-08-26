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
  AL_ETrackDmaMode eTrackDmaMode = AL_ETrackDmaMode::AL_TRACK_DMA_MODE_NONE;
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
  AL_TAllocator* GetAllocator();
  AL_ITimer* GetTimer();

  CIpDevice(CIpDevice const &) = delete;
  CIpDevice & operator = (CIpDevice const &) = delete;

private:
  AL_IEncScheduler* m_pScheduler = nullptr;
  std::shared_ptr<AL_TAllocator> m_pAllocator = nullptr;
  AL_ITimer* m_pTimer = nullptr;

  void ConfigureMcu(CIpDeviceParam& param);
};

inline AL_IEncScheduler* CIpDevice::GetScheduler(void)
{
  return m_pScheduler;
}

inline AL_TAllocator* CIpDevice::GetAllocator(void)
{
  return m_pAllocator.get();
}

inline AL_ITimer* CIpDevice::GetTimer(void)
{
  return m_pTimer;
}

