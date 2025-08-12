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

extern "C" {
#include "config.h"
#include "lib_decode/LibDecoderRiscv.h"
#include "lib_common/Allocator.h"
#include "lib_common/Context.h"
}

#include <memory>
#include <sstream>

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
} // namespace anonymous

#ifdef HAVE_VCU2_CTRLSW
class VCU2Device : public Device
{
public:
    ~VCU2Device() override;
    VCU2Device();

    void* getScheduler() override { return nullptr; }
    void* getCtx() override { return ctx_; }
    AL_TAllocator* getAllocator() override { return allocator_; }
    AL_ITimer* getTimer() override { return nullptr; };

private:
    void configureRiscv();
    AL_TAllocator* allocator_;
    AL_RiscV_Ctx ctx_;
};

VCU2Device::VCU2Device()
{
    configureRiscv();
}

VCU2Device::~VCU2Device()
{
    if (ctx_)
        AL_Riscv_Decode_DestroyCtx(ctx_);
    if (allocator_)
        AL_Allocator_Destroy(allocator_);
}

void VCU2Device::configureRiscv(void)
{
    uint32_t const sw_version =
        (uint32_t)((AL_VERSION_MAJOR << 20) + (AL_VERSION_MINOR << 12) + (AL_VERSION_PATCH));
    uint32_t fw_version;

    std::string defaultdev = DECODER_DEVICES;
    ctx_ = AL_Riscv_Decode_CreateCtx(defaultdev.c_str());

    if (!ctx_)
        throw std::runtime_error("Failed to create context");

    fw_version = AL_Riscv_Decode_Get_FwVersion(ctx_);
    if (!fw_version || (fw_version != sw_version))
        throw std::runtime_error("FW Version " + versionToStr(fw_version) + ", it should be "
                                 + versionToStr(sw_version));

    allocator_ = AL_Riscv_Decode_DmaAlloc_Create(ctx_);
    if (!allocator_)
        throw std::runtime_error("Can't find dma allocator");

}

#endif // HAVE_VCU2_CTRLSW


/*static*/ Ptr<Device> Device::create() {
#ifdef HAVE_VCU2_CTRLSW
    return Ptr<Device>(new VCU2Device());
#endif
#ifdef HAVE_VCU_CTRLSW
    return Ptr<Device>(nullptr);
#endif
#ifdef HAVE_VDU_CTRLSW
    return Ptr<Device>(nullptr);
#endif
}


} // namespace vcucodec
} // namespace cv

