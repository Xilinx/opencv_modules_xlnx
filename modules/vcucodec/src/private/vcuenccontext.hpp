
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
#ifndef OPENCV_VCUCODEC_VCUENCCONTEXT_HPP
#define OPENCV_VCUCODEC_VCUENCCONTEXT_HPP

#include <opencv2/core.hpp>

#include "lib_app/InputFiles.hpp"
#include "lib_app/utils.hpp"

#include "config.h"

extern "C"
{
#include "lib_encode/lib_encoder.h"
#include "lib_common_enc/Settings.h"
#include "lib_common_enc/RateCtrlMeta.h"
}

#include <memory>
#include <vector>

struct ConfigFile;
struct AL_TBuffer;

namespace cv {
namespace vcucodec {

class Device;

using DataCallback = std::function<void(std::vector<std::string_view>&)>;

class EncContext
{
public:
    class Config;

    virtual ~EncContext() = default;
    virtual void writeFrame(std::shared_ptr<AL_TBuffer> frame) = 0;
    virtual std::shared_ptr<AL_TBuffer> getSharedBuffer() = 0;
    virtual bool waitForCompletion() = 0;
    virtual void notifyGMV(int32_t frameIndex, int32_t gmVectorX, int32_t gmVectorY) = 0;
    virtual String statistics() const = 0;
    virtual AL_HEncoder hEnc() = 0;

    static Ptr<EncContext> create(Ptr<Config> cfg, Ptr<Device>& device, DataCallback dataCallback);
};

struct ConfigRunInfo
{
    std::vector<std::string> encDevicePaths;
    AL_EDeviceType eDeviceType;
    AL_ESchedulerType eSchedulerType;
    bool bLoop;
    int32_t iMaxPict;
    unsigned int iFirstPict;
    unsigned int iScnChgLookAhead;
    std::string sRecMd5Path;
    std::string sStreamMd5Path;
    AL_EIpCtrlMode ipCtrlMode;
    std::string logsFile = "";
    std::string apbFile = "";
#ifdef HAVE_VCU2_CTRLSW
    bool trackDma = false;
#else
    AL_ETrackDmaMode eTrackDmaMode = AL_ETrackDmaMode::AL_TRACK_DMA_MODE_NONE;
#endif
    bool printPictureType = false;
    AL_ERateCtrlStatMode rateCtrlStat = AL_RATECTRL_STAT_MODE_NONE;
    std::string rateCtrlMetaPath = "";
    std::string bitrateFile = "";
    AL_64U uInputSleepInMilliseconds;
#ifdef HAVE_VCU2_CTRLSW
    bool bEmulateSrcSync = false;
#endif
};

struct ConfigYUVInput
{
    std::string YUVFileName;  ///< Input YUV file name
    std::string sMapFileName; ///< Map file name used when encoder receives a compressed YUV file.
    AL_TYUVFileInfo FileInfo; ///< Information related to the YUV input file
};

typedef enum
{
    AL_SRC_FORMAT_RASTER,
#ifdef HAVE_VCU2_CTRLSW
    AL_SRC_FORMAT_RASTER_MSB,
#endif
    AL_SRC_FORMAT_TILE_64x4,
    AL_SRC_FORMAT_TILE_32x4,
    AL_SRC_FORMAT_COMP_64x4,
    AL_SRC_FORMAT_COMP_32x4,
    AL_SRC_FORMAT_MAX_ENUM,
} AL_ESrcFormat;

struct EncContext::Config
{
    ConfigYUVInput MainInput; ///< Main YUV input
    std::vector<ConfigYUVInput> DynamicInputs; ///<List of inputs for resolution change
    std::string RecFileName;  ///< Reconstructed YUV output file name
    TFourCC RecFourCC;        ///< FOURCC Code of the reconstructed picture output file
    AL_ESrcFormat eSrcFormat; ///< Source format of encoder input
    AL_TEncSettings Settings; ///< Rate control and other encoder settings
    ConfigRunInfo RunInfo;    ///< Runtime information
    int32_t iForceStreamBufSize = 0; ///< Force stream buffer size (0 = automatic)
};

} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUENCCONTEXT_HPP
