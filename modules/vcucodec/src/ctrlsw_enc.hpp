// Modifications Copyright(C) [2025] Advanced Micro Devices, Inc. All rights reserved)

// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "lib_common/PicFormat.h"
#include <climits>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <dirent.h>
#endif

#if defined(_WIN32)
#include "extra/dirent/include/dirent.h"
#endif

#include "lib_app/BufPool.h"
#include "lib_app/FileUtils.h"
#include "lib_app/PixMapBufPool.h"
#include "lib_app/YuvIO.h"
#include "lib_app/console.h"
#include "lib_app/plateform.h"
#include "lib_app/utils.h"
#ifdef HAVE_VCU2_CTRLSW
#include "lib_app/CompFrameReader.h"
#include "lib_app/CompFrameCommon.h"
#endif
#include "lib_app/UnCompFrameReader.h"
#include "lib_app/SinkFrame.h"

#include "CfgParser.h"
#include "CodecUtils.h"
#include "IpDevice.h"
#include "resource.h"

extern "C" {
#include "lib_common/BufferPictureMeta.h"
#include "lib_common/BufferStreamMeta.h"
#include "lib_common/Error.h"
#include "lib_common/PixMapBuffer.h"
#include "lib_common/RoundUp.h"
#include "lib_common/StreamBuffer.h"
#include "lib_common_enc/RateCtrlMeta.h"
#include "lib_encode/lib_encoder.h"
#include "lib_rtos/lib_rtos.h"
#include "lib_common_enc/EncBuffers.h"
#include "lib_common_enc/IpEncFourCC.h"
}

#include "lib_app/AL_RasterConvert.h"

#include "sink_encoder.h"
#include "sink_yuv_md5.h"
//#include "sink_ratectrl_meta.h"
#include "sink_lookahead.h"
//#include "QPGenerator.h"
#include "lib_app/SinkStreamMd5.h"
#include "sink_bitrate.h"
#include "sink_bitstream_writer.h"
#include "sink_repeater.h"

//#include "RCPlugin.h"

using namespace std;

/*****************************************************************************/
#include "lib_app/BuildInfo.h"

#if !defined(HAS_COMPIL_FLAGS)
#define AL_COMPIL_FLAGS ""
#endif

/*****************************************************************************/

struct SrcConverterParams
{
  AL_TDimension tDim;
  TFourCC tFileFourCC;
  AL_TPicFormat tSrcPicFmt;
  AL_ESrcFormat eSrcFormat;
};

/*****************************************************************************/
struct SrcBufChunk
{
  int32_t iChunkSize;
  std::vector<AL_TPlaneDescription> vPlaneDesc;
};

struct SrcBufDesc
{
  TFourCC tFourCC;
  std::vector<SrcBufChunk> vChunks;
};

/*****************************************************************************/
struct LayerResources
{
  void Init(ConfigFile& cfg, AL_TEncoderInfo tEncInfo, int32_t iLayerID, CIpDevice* pDevices, int32_t chanId);

  void PushResources(ConfigFile& cfg, EncoderSink* enc
                     , EncoderLookAheadSink* encFirstPassLA
                     );

  void OpenEncoderInput(ConfigFile& cfg, AL_HEncoder hEnc);

  bool SendInput(ConfigFile& cfg, IFrameSink* firstSink, void* pTraceHook);

  bool sendInputFileTo(unique_ptr<FrameReader>& frameReader, PixMapBufPool& SrcBufPool, AL_TBuffer* Yuv, ConfigFile const& cfg, AL_TYUVFileInfo& FileInfo, IConvSrc* pSrcConv, IFrameSink* sink, int& iPictCount, int& iReadCount);

  unique_ptr<FrameReader> InitializeFrameReader(ConfigFile& cfg, ifstream& YuvFile, string sYuvFileName, ifstream& MapFile, string sMapFileName, AL_TYUVFileInfo& FileInfo);

  void ChangeInput(ConfigFile& cfg, int32_t iInputIdx, AL_HEncoder hEnc);

  BufPool StreamBufPool;
  BufPool QpBufPool;
  PixMapBufPool SrcBufPool;

  // Input/Output Format conversion
  ifstream YuvFile;
  ifstream MapFile;
  unique_ptr<FrameReader> frameReader;
  unique_ptr<IConvSrc> pSrcConv;
  shared_ptr<AL_TBuffer> SrcYuv;

  vector<uint8_t> RecYuvBuffer;
  unique_ptr<IFrameSink> frameWriter;

  int32_t iPictCount = 0;
  int32_t iReadCount = 0;

  int32_t iLayerID = 0;
  int32_t iInputIdx = 0;
  vector<TConfigYUVInput> layerInputs;
};

/*****************************************************************************/
void SetDefaults(ConfigFile& cfg);
void SetCodingResolution(ConfigFile& cfg);
unique_ptr<EncoderSink> CtrlswEncOpen(ConfigFile& cfg, std::vector<std::unique_ptr<LayerResources>>& pLayerResources, shared_ptr<CIpDevice>& pIpDevice);

