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

#include "vcudevice.hpp"

const char* err = "only one of HAVE_VCU2_CTRLSW, HAVE_VCU_CTRLSW, HAVE_VDU_CTRLSW can be defined";
#if defined(HAVE_VCU_CTRLSW) && (defined(HAVE_VCU2_CTRLSW) || defined(HAVE_VDU_CTRLSW))
static_assert(false, err);
#endif
#if defined(HAVE_VCU2_CTRLSW) && (defined(HAVE_VCU_CTRLSW) || defined(HAVE_VDU_CTRLSW))
static_assert(false, err);
#endif
#if defined(HAVE_VDU_CTRLSW) && (defined(HAVE_VCU2_CTRLSW) || defined(HAVE_VCU_CTRLSW))
static_assert(false, err);
#endif

#ifdef HAVE_VCU_CTRLSW
#include "lib_app/AllocatorHelper.h"
#endif

extern "C" {
#include "config.h"
#ifdef HAVE_VCU2_CTRLSW
#include "lib_decode/LibDecoderRiscv.h"
#include "lib_encode/LibEncoderRiscv.h"
#endif

#include "lib_common/Allocator.h"
#ifdef HAVE_VCU2_CTRLSW
#include "lib_common/Context.h"
#endif
#ifdef HAVE_VCU_CTRLSW
//#include "lib_common/IDriver.h"
#include "lib_common/HardwareDriver.h"
#include "lib_common_dec/DecBuffers.h"
#include "lib_common_dec/IpDecFourCC.h"
#include "lib_decode/lib_decode.h"
#include "lib_decode/DecSchedulerMcu.h"
#include "lib_encode/lib_encoder.h"
#include "lib_encode/EncSchedulerMcu.h"
#include "lib_common/HardwareDriver.h"
#endif
}

#include <memory>
#include <sstream>
#include <string_view>

namespace cv {
namespace vcucodec {

namespace { // anonymous

const std::string versionToStr(uint32_t const& version)
{
    uint8_t major = static_cast<uint8_t>(version >> 20);
    uint8_t minor = static_cast<uint8_t>(version >> 12);
    uint8_t patch = static_cast<uint8_t>(version);
    std::stringstream ss;
    ss << std::to_string(static_cast<unsigned int>(major)) << ".";
    ss << std::to_string(static_cast<unsigned int>(minor)) << ".";
    ss << std::to_string(static_cast<unsigned int>(patch));

    return ss.str();
}

std::string_view deviceID(Device::ID id)
{
    static std::string emptyString = "";
    std::string_view deviceString;
    std::vector<std::string_view> decoder_devices = DECODER_DEVICES;
    std::vector<std::string_view> encoder_devices = ENCODER_DEVICES;
    switch (id)
    {
    case Device::DECODER0:
        deviceString = decoder_devices[0];
        break;
    case Device::DECODER1:
        if (decoder_devices.size() > 1)
            deviceString = decoder_devices[1];
        break;
    case Device::ENCODER0:
        deviceString = encoder_devices[0];
        break;
    case Device::ENCODER1:
        if (encoder_devices.size() > 1)
            deviceString = encoder_devices[1];
        break;
    default:
        deviceString = emptyString;
        break;
    }
    return deviceString;
}

} // namespace anonymous

#ifdef HAVE_VCU2_CTRLSW
class VCU2DecDevice : public Device
{
public:
    ~VCU2DecDevice() override;
    VCU2DecDevice(Device::ID);

    VCU2DecDevice(VCU2DecDevice const &) = delete;
    VCU2DecDevice & operator = (VCU2DecDevice const &) = delete;

    void* getScheduler() override { return nullptr; }
    void* getCtx() override { return ctx_; }
    AL_TAllocator* getAllocator() override { return allocator_.get(); }
    AL_ITimer* getTimer() override { return nullptr; };

private:
    std::shared_ptr<AL_TAllocator> allocator_;
    AL_RiscV_Ctx ctx_;
};

VCU2DecDevice::VCU2DecDevice(Device::ID id)
{
    uint32_t const sw_version =
        (uint32_t)((AL_VERSION_MAJOR << 20) + (AL_VERSION_MINOR << 12) + (AL_VERSION_PATCH));
    uint32_t fw_version;

    if (id & Device::DECODER0)
    {
        std::string_view defaultdev = deviceID(DECODER0);
        if (!defaultdev.empty())
            ctx_ = AL_Riscv_Decode_CreateCtx(defaultdev.data());
    }

    if (!ctx_ && (id & Device::DECODER1))
    {
        std::string_view defaultdev = deviceID(DECODER1);
        if (!defaultdev.empty())
            ctx_ = AL_Riscv_Decode_CreateCtx(defaultdev.data());
    }

    if (!ctx_)
        throw std::runtime_error("Device not found");

    if (!ctx_)
        throw std::runtime_error("Failed to create context");

    fw_version = AL_Riscv_Decode_Get_FwVersion(ctx_);
    if (!fw_version || (fw_version != sw_version))
        throw std::runtime_error("FW Version " + versionToStr(fw_version) + ", it should be "
                                 + versionToStr(sw_version));

    AL_TAllocator* allocator = AL_Riscv_Decode_DmaAlloc_Create(ctx_);
    if (!allocator)
        throw std::runtime_error("Can't find dma allocator");

    allocator_.reset(allocator, &AL_Allocator_Destroy);
}

VCU2DecDevice::~VCU2DecDevice()
{
    if (ctx_)
        AL_Riscv_Decode_DestroyCtx(ctx_);
}

class VCU2EncDevice : public Device
{
public:
    ~VCU2EncDevice() override;
    VCU2EncDevice(Device::ID);

    VCU2EncDevice(VCU2EncDevice const &) = delete;
    VCU2EncDevice & operator = (VCU2EncDevice const &) = delete;

    void* getScheduler() override { return nullptr; }
    void* getCtx() override { return ctx_; }
    AL_TAllocator* getAllocator() override { return allocator_.get(); }
    AL_ITimer* getTimer() override { return nullptr; };

private:
    std::shared_ptr<AL_TAllocator> allocator_;
    AL_RiscV_Ctx ctx_;
};

VCU2EncDevice::VCU2EncDevice(Device::ID id)
{
    uint32_t const sw_version =
        (uint32_t)((AL_VERSION_MAJOR << 20) + (AL_VERSION_MINOR << 12) + (AL_VERSION_PATCH));
    uint32_t fw_version;

    if (id & Device::ENCODER0)
    {
        std::string_view defaultdev = deviceID(ENCODER0);
        if (!defaultdev.empty())
            ctx_ = AL_Riscv_Encode_CreateCtx(defaultdev.data());
    }
    if (!ctx_ && (id & Device::ENCODER1))
    {
        std::string_view defaultdev = deviceID(ENCODER1);
        if (!defaultdev.empty())
            ctx_ = AL_Riscv_Encode_CreateCtx(defaultdev.data());
    }

    if (!ctx_)
        throw std::runtime_error("Failed to create context");

    fw_version = AL_Riscv_Encode_Get_FwVersion(ctx_);
    if (!fw_version || (fw_version != sw_version))
        throw std::runtime_error("FW Version " + versionToStr(fw_version) + ", it should be "
                                 + versionToStr(sw_version));

    AL_TAllocator* allocator = AL_Riscv_Encode_DmaAlloc_Create(ctx_);
    if (!allocator)
        throw std::runtime_error("Can't find dma allocator");
    allocator_.reset(allocator, &AL_Allocator_Destroy);
}

VCU2EncDevice::~VCU2EncDevice()
{
    if (ctx_)
        AL_Riscv_Encode_DestroyCtx(ctx_);
}

#endif // HAVE_VCU2_CTRLSW

#ifdef HAVE_VCU_CTRLSW
class VCUDecDevice : public Device
{
public:
    ~VCUDecDevice() override;
    VCUDecDevice(Device::ID);

    void* getScheduler() override { return scheduler_; }
    AL_TAllocator* getAllocator() override { return allocator_.get(); }
	void* getCtx() override { return nullptr; }
    AL_ITimer* getTimer() override { return nullptr; };

private:
    void configureMcu(AL_TDriver* driver);
    AL_IDecScheduler* scheduler_ = nullptr;
    std::shared_ptr<AL_TAllocator> allocator_ = nullptr;
};

VCUDecDevice::VCUDecDevice(Device::ID)
{
    configureMcu(AL_GetHardwareDriver());
}

VCUDecDevice::~VCUDecDevice()
{
    if (scheduler_)
        AL_IDecScheduler_Destroy(scheduler_);
}

void VCUDecDevice::configureMcu(AL_TDriver* driver)
{
  std::string g_DecDevicePath = "/dev/allegroDecodeIP";
  allocator_ = CreateBoardAllocator(g_DecDevicePath.c_str(), AL_ETrackDmaMode::AL_TRACK_DMA_MODE_NONE);

  if(!allocator_)
    throw std::runtime_error("Can't open DMA allocator");

  scheduler_ = AL_DecSchedulerMcu_Create(driver, g_DecDevicePath.c_str());

  if(!scheduler_)
    throw std::runtime_error("Failed to create MCU scheduler");
}

class VCUEncDevice : public Device
{
public:
    ~VCUEncDevice() override;
    VCUEncDevice(Device::ID);

    void* getScheduler() override { return scheduler_; }
    AL_TAllocator* getAllocator() override { return allocator_.get(); }
	void* getCtx() override { return nullptr; }
    AL_ITimer* getTimer() override { return nullptr; };

private:
    void configureMcu(AL_TDriver* driver);
    AL_IEncScheduler* scheduler_ = nullptr;
    std::shared_ptr<AL_TAllocator> allocator_ = nullptr;
};

VCUEncDevice::VCUEncDevice(Device::ID)
{
    configureMcu(AL_GetHardwareDriver());
}

VCUEncDevice::~VCUEncDevice()
{
    if (scheduler_)
        AL_IEncScheduler_Destroy(scheduler_);
}

void VCUEncDevice::configureMcu(AL_TDriver* driver)
{
  std::string g_EncDevicePath = "/dev/allegroIP";
  allocator_ = CreateBoardAllocator(g_EncDevicePath.c_str(), AL_ETrackDmaMode::AL_TRACK_DMA_MODE_NONE);

  if(!allocator_)
    throw std::runtime_error("Can't open DMA allocator");

  scheduler_ = AL_SchedulerMcu_Create(AL_GetHardwareDriver(),
      (AL_TLinuxDmaAllocator*)allocator_.get(), g_EncDevicePath.c_str());

  if(!scheduler_)
    throw std::runtime_error("Failed to create MCU scheduler");
}
#endif // HAVE_VCU_CTRLSW

/*static*/ Ptr<Device> Device::create(Device::ID id)
{
#ifdef HAVE_VCU2_CTRLSW
    switch (id)
    {
    case DECODER0:
    case DECODER1:
    case DECODER:
        return Ptr<Device>(new VCU2DecDevice(id));
    case ENCODER0:
    case ENCODER1:
    case ENCODER:
        return Ptr<Device>(new VCU2EncDevice(id));
    }
#endif
#ifdef HAVE_VCU_CTRLSW
    switch (id)
    {
    case DECODER0:
    case DECODER1:
    case DECODER:
        return Ptr<Device>(new VCUDecDevice(id));
    case ENCODER0:
    case ENCODER1:
    case ENCODER:
        return Ptr<Device>(new VCUEncDevice(id));
    }
#endif
#ifdef HAVE_VDU_CTRLSW
#endif
    return Ptr<Device>(nullptr);
}


} // namespace vcucodec
} // namespace cv

