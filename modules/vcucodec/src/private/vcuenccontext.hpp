
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
#include "QPGenerator.h"

#include "config.h"

extern "C"
{
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
    AL_EGenerateQpMode eGenerateQpMode = AL_GENERATE_UNIFORM_QP;
#ifdef HAVE_VCU2_CTRLSW
    bool bEmulateSrcSync = false;
#endif
};

struct ConfigYUVInput
{
    // \brief YUV input file name(s)
    std::string YUVFileName;

    // \brief Map file name used when the encoder receives a compressed YUV file.
    std::string sMapFileName;

    // \brief Information relative to the YUV input file
    AL_TYUVFileInfo FileInfo;

    // \brief Folder where qp tables files are located, if load qp enabled.
    std::string sQPTablesFolder;

    // \brief Name of the file specifying the region of interest per frame is specified
    // happen
    std::string sRoiFileName;
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
    // \brief Path to the cfg location
    std::string sCfgPath;

    // \brief Main YUV input
    ConfigYUVInput MainInput;

    // \brief List of inputs for resolution change
    std::vector<ConfigYUVInput> DynamicInputs;

    // \brief Output bitstream file name
    std::string BitstreamFileName;

    // \brief Reconstructed YUV output file name
    std::string RecFileName;

    // \brief Name of the file specifying the frame numbers where scene changes
    // happen
    std::string sCmdFileName;

#ifdef HAVE_VCU2_CTRLSW
    // \brief Name of the file specifying Global Motion Vector for each frame
    std::string sGMVFileName;
#endif
    // \brief Name of the file that reads/writes video statistics for TwoPassMode
    std::string sTwoPassFileName;

    // \brief Name of the file specifying HDR SEI contents
    std::string sHDRFileName;

    // \brief FOURCC Code of the reconstructed picture output file
    TFourCC RecFourCC;

    // \brief Source format of encoder input
    AL_ESrcFormat eSrcFormat;

    // \brief Sections RATE_CONTROL and SETTINGS
    AL_TEncSettings Settings;

    // \brief Section RUN
    ConfigRunInfo RunInfo;
#ifdef HAVE_VCU2_CTRLSW
    // \brief maximum burst size
    int32_t iEncMaxAxiBurstSize = 0;
#endif
    // \brief control the strictness when parsing the configuration file
    bool strict_mode;

    int32_t iForceStreamBufSize = 0;
};

} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUENCCONTEXT_HPP
