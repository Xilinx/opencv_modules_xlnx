// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#include <fstream>
#include <string>
#include <stdexcept>

#include "sink_ratectrl_meta.h"
#include "lib_app/Sink.h"
#include "lib_app/utils.h"
#include "lib_app/FileUtils.h"

extern "C" {
#include "lib_common/BufferAPI.h"
#include "lib_common_enc/RateCtrlMeta.h"
}

class SinkRateCtrlMeta : public IFrameSink
{
private:
  std::string output_dir_path;
  std::ofstream m_MVFile;
  std::ofstream m_RateCtrlStatsFile;

  void checkOpenFiles(AL_ERateCtrlStatMode eMode)
  {
    if((eMode & AL_RATECTRL_STAT_MODE_MV) && !m_MVFile.is_open())
    {
      std::string mv_file_path = output_dir_path;
      OpenOutput(m_MVFile, mv_file_path.append("/motion_vectors.bin"), false);
    }

    if((eMode & AL_RATECTRL_STAT_MODE_DEFAULT) && !m_RateCtrlStatsFile.is_open())
    {
      std::string stats_file_path = output_dir_path;
      OpenOutput(m_RateCtrlStatsFile, stats_file_path.append("/rate_ctrl_stats.txt"), false);
    }
  }

public:
  explicit SinkRateCtrlMeta(std::string const& path)
  {
    if(path.empty())
      throw std::runtime_error("Output directory for stat is not set");
    else if(!checkFolder(path))
      throw std::runtime_error("Output directory for stat does not exist");
    else
      output_dir_path = path;
  }

  void ProcessFrame(AL_TBuffer* pBuf) override
  {
    AL_TRateCtrlMetaData* pMeta = (AL_TRateCtrlMetaData*)AL_Buffer_GetMetaData(pBuf, AL_META_TYPE_RATECTRL);
    checkOpenFiles(pMeta->eStatCtrl);

    if(pMeta != NULL)
    {
      if(pMeta->eStatCtrl & AL_RATECTRL_STAT_MODE_DEFAULT)
      {
        m_RateCtrlStatsFile << "NumBytes: " << pMeta->tRateCtrlStats.uNumBytes << ", MinQP: " << pMeta->tRateCtrlStats.uMinQP << ", MaxQP: " << pMeta->tRateCtrlStats.uMaxQP << ", NumSkip: " << pMeta->tRateCtrlStats.uNumSkip << ", NumIntra: " << pMeta->tRateCtrlStats.uNumIntra << "\n";
      }

      if(pMeta->eStatCtrl & AL_RATECTRL_STAT_MODE_MV)
      {
        if(pMeta->pMVBuf != NULL)
        {
          size_t s = AL_Buffer_GetSize(pMeta->pMVBuf);
          const uint8_t* pData = AL_Buffer_GetData(pMeta->pMVBuf);
          m_MVFile.write((const char*)pData, s);
        }
      }
    }
  }
};

IFrameSink* createRateCtrlMetaSink(std::string const& path)
{
  return new SinkRateCtrlMeta(path);
}
