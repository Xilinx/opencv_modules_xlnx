// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#include "sink_bitstream_writer.h"
#include "lib_app/utils.h" // OpenOutput
#include "lib_app/InputFiles.h"
#include "CodecUtils.h" // WriteStream
#include <fstream>

extern "C"
{
}
using namespace std;

void WriteContainerHeader(ofstream& fp, AL_TEncSettings const& Settings, AL_TYUVFileInfo const& FileInfo, int32_t numFrames);

struct BitstreamWriter : IFrameSink
{
  BitstreamWriter(string path, ConfigFile const& cfg_) : cfg(cfg_)
  {
    OpenOutput(m_file, path);
    WriteContainerHeader(m_file, cfg.Settings, cfg.MainInput.FileInfo, -1);
  }

  ~BitstreamWriter(void)
  {
    printBitrate();

    // update container header
    WriteContainerHeader(m_file, cfg.Settings, cfg.MainInput.FileInfo, m_frameCount);
    m_file.flush();
  }

  void ProcessFrame(AL_TBuffer* pStream) override
  {
    if(pStream == nullptr)
      return;

    m_frameCount += WriteStream(m_file, pStream, &cfg.Settings, hdr_pos, m_iFrameSize);
  }

  void printBitrate(void)
  {
    auto const outputSizeInBits = m_file.tellp() * 8;
    auto const frameRate = (float)cfg.Settings.tChParam[0].tRCParam.uFrameRate / cfg.Settings.tChParam[0].tRCParam.uClkRatio;
    auto const durationInSeconds = m_frameCount / (frameRate * cfg.Settings.NumLayer);
    auto bitrate = outputSizeInBits / durationInSeconds;
    LogInfo("Achieved bitrate = %.4f Kbps\n", (float)bitrate);
  }

  int32_t m_frameCount = 0;
  ofstream m_file;
  streampos hdr_pos;
  int32_t m_iFrameSize = 0;
  ConfigFile const cfg;
};

IFrameSink* createBitstreamWriter(string path, ConfigFile const& cfg)
{

  if(cfg.Settings.TwoPass == 1)
    return new NullFrameSink;

  return new BitstreamWriter(path, cfg);
}
