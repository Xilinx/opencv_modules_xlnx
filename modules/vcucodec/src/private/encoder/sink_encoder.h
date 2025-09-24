// Modifications Copyright(C) [2025] Advanced Micro Devices, Inc. All rights reserved)

// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once
#include <cassert>

#include "lib_app/BufPool.hpp"
#include "lib_app/timing.hpp"
#include "lib_app/Sink.hpp"
#include "lib_app/convert.hpp"
#include "lib_app/YuvIO.hpp"

extern "C"
{
#include "lib_common/BufferPictureMeta.h"
#include "lib_common/BufferStreamMeta.h"
}

#ifdef HAVE_VCU2_CTRLSW
extern "C"
{
#include "lib_common/FbcMapSize.h"
#include "lib_common_enc/EncChanParam.h"
}
#endif

#include <string>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <map>
#include <functional>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "RCPlugin.h"
extern "C" {
#include "lib_common_enc/RateCtrlMeta.h"
}

#include "../vcudata.hpp"
#include "../vcuutils.hpp"
#include "../vcuenccontext.hpp"

#define NUM_PASS_OUTPUT 1

#define MAX_NUM_REC_OUTPUT (MAX_NUM_LAYER > NUM_PASS_OUTPUT ? MAX_NUM_LAYER : NUM_PASS_OUTPUT)
#define MAX_NUM_BITSTREAM_OUTPUT NUM_PASS_OUTPUT

using DataCallback = std::function<void (std::vector<std::string_view>&)>;
using en_codec_error = cv::vcucodec::en_codec_error;
using ChangeSourceCallback = std::function<void(int, int)>;

static std::string PictTypeToString(AL_ESliceType type)
{
  std::map<AL_ESliceType, std::string> m =
  {
    { AL_SLICE_B, "B" },
    { AL_SLICE_P, "P" },
    { AL_SLICE_I, "I" },
    { AL_SLICE_GOLDEN, "Golden" },
    { AL_SLICE_CONCEAL, "Conceal" },
    { AL_SLICE_SKIP, "Skip" },
    { AL_SLICE_REPEAT, "Repeat" },
  };

  return m.at(type);
}

struct EncoderSink
{
#ifdef HAVE_VCU2_CTRLSW
  explicit EncoderSink(cv::vcucodec::EncContext::Config const& cfg, AL_RiscV_Ctx ctx, AL_TAllocator* pAllocator) :
    m_cfg(cfg),
    pAllocator{pAllocator},
    pSettings{&cfg.Settings}
  {
    assert(ctx);
    AL_CB_EndEncoding onEncoding = { &EncoderSink::EndEncoding, this };

    AL_ERR errorCode = AL_Encoder_CreateWithCtx(&hEnc, ctx, this->pAllocator, &cfg.Settings, onEncoding);

    if(AL_IS_ERROR_CODE(errorCode))
      throw en_codec_error(AL_Codec_ErrorToString(errorCode), errorCode);

    if(AL_IS_WARNING_CODE(errorCode))
      LogWarning("%s\n", AL_Codec_ErrorToString(errorCode));

    for(int32_t i = 0; i < MAX_NUM_REC_OUTPUT; ++i)
      RecOutput[i].reset(new NullFrameSink);

    for(int32_t i = 0; i < MAX_NUM_LAYER; i++)
      m_input_picCount[i] = 0;

    m_pictureType = cfg.RunInfo.printPictureType ? AL_SLICE_MAX_ENUM : -1;

    iPendingStreamCnt = 1;
  }
#endif

  explicit EncoderSink(cv::vcucodec::EncContext::Config const& cfg, AL_IEncScheduler* pScheduler, AL_TAllocator* pAllocator) :
    m_cfg(cfg),
    pAllocator{pAllocator},
    pSettings{&cfg.Settings}
  {
    AL_CB_EndEncoding onEncoding = { &EncoderSink::EndEncoding, this };

    AL_ERR errorCode = AL_Encoder_Create(&hEnc, pScheduler, this->pAllocator, &cfg.Settings, onEncoding);

    if(AL_IS_ERROR_CODE(errorCode))
      throw en_codec_error(AL_Codec_ErrorToString(errorCode), errorCode);

    if(AL_IS_WARNING_CODE(errorCode))
      LogWarning("%s\n", AL_Codec_ErrorToString(errorCode));

    for(int32_t i = 0; i < MAX_NUM_REC_OUTPUT; ++i)
      RecOutput[i].reset(new NullFrameSink);

    for(int32_t i = 0; i < MAX_NUM_LAYER; i++)
      m_input_picCount[i] = 0;

    m_pictureType = cfg.RunInfo.printPictureType ? AL_SLICE_MAX_ENUM : -1;

    // TODO: AL_Encoder_SetHDRSEIs(hEnc, &tHDRSEIs);

    iPendingStreamCnt = 1;

  }

  ~EncoderSink(void)
  {
    LogInfo("%d pictures encoded. Average FrameRate = %.4f Fps\n",
            m_input_picCount[0], (m_input_picCount[0] * 1000.0) / (m_EndTime - m_StartTime));

    AL_Encoder_Destroy(hEnc);
  }

  void SetChangeSourceCallback(ChangeSourceCallback changeSourceCB)
  {
    m_changeSourceCB = changeSourceCB;
  }

  bool waitForCompletion(void)
  {
    std::unique_lock<std::mutex> lock(encoding_complete_mutex);
    return encoding_complete_cv.wait_for(lock, std::chrono::seconds(1), [this] { return encoding_finished; });
  }

  // Synchronization for blocking eos()
  std::mutex encoding_complete_mutex;
  std::condition_variable encoding_complete_cv;
  bool encoding_finished = false;


  void PreprocessFrame()
  {
  }

  void ProcessFrame(AL_TBuffer* Src)
  {
    if(m_input_picCount[0] == 0)
      m_StartTime = GetPerfTime();

    if(!Src)
    {
      LogVerbose("Flushing...\n\n");

      if(!AL_Encoder_Process(hEnc, nullptr, nullptr))
        CheckErrorAndThrow();
      return;
    }

    LogVerbose("\r  Encoding picture #%-6d - ", m_input_picCount[0]);
    fflush(stdout);

    CheckSourceResolutionChanged(Src);

    if(pSettings->hRcPluginDmaContext != NULL)
      RCPlugin_SetNextFrameQP(pSettings, this->pAllocator);

    if(!AL_Encoder_Process(hEnc, Src, nullptr))
      CheckErrorAndThrow();

    m_input_picCount[0]++;
  }

  AL_ERR GetLastError(void)
  {
    return m_EncoderLastError;
  }

  std::unique_ptr<IFrameSink> RecOutput[MAX_NUM_REC_OUTPUT];
  DataCallback dataCallback_;
  AL_HEncoder hEnc;
  bool shouldAddDummySei = false;

private:
  int32_t iPendingStreamCnt;
  int32_t m_input_picCount[MAX_NUM_LAYER] {};
  int32_t m_pictureType = -1;
  uint64_t m_StartTime = 0;
  uint64_t m_EndTime = 0;
  cv::vcucodec::EncContext::Config const& m_cfg;

  AL_TAllocator* pAllocator;
  AL_TEncSettings const* pSettings;
  ChangeSourceCallback m_changeSourceCB;
  AL_TDimension tLastEncodedDim;
  AL_ERR m_EncoderLastError = AL_SUCCESS;

  void CheckErrorAndThrow(void)
  {
    AL_ERR eErr = AL_Encoder_GetLastError(hEnc);
    throw std::runtime_error(AL_IS_ERROR_CODE(eErr) ? AL_Codec_ErrorToString(eErr) : "Failed");
  }

  static inline bool isStreamReleased(AL_TBuffer* pStream, AL_TBuffer const* pSrc)
  {
    return pStream && !pSrc;
  }

  static inline bool isSourceReleased(AL_TBuffer* pStream, AL_TBuffer const* pSrc)
  {
    return !pStream && pSrc;
  }

  static void EndEncoding(void* userParam, AL_TBuffer* pStream, AL_TBuffer const* pSrc, int)
  {
    auto pThis = (EncoderSink*)userParam;

    if(isStreamReleased(pStream, pSrc) || isSourceReleased(pStream, pSrc))
      return;

    cv::Ptr<cv::vcucodec::Data> data = cv::vcucodec::Data::create(pStream, pThis->hEnc);
    pThis->processOutput(data);
  }

  void ComputeQualityMeasure(AL_TRateCtrlMetaData* pMeta)
  {
    if(!pMeta->bFilled)
      return;
  }

  void AddSei(AL_TBuffer* pStream, bool isPrefix, int32_t payloadType, uint8_t* payload, int32_t payloadSize, int32_t tempId)
  {
    int32_t seiSection = AL_Encoder_AddSei(hEnc, pStream, isPrefix, payloadType, payload, payloadSize, tempId);

    if(seiSection < 0)
      LogWarning("Failed to add dummy SEI (id:%d) \n", seiSection);
  }

  AL_ERR PreprocessOutput(cv::Ptr<cv::vcucodec::Data> pStream)
  {
    AL_ERR eErr = AL_Encoder_GetLastError(hEnc);

    if(AL_IS_ERROR_CODE(eErr))
    {
      LogError("%s\n", AL_Codec_ErrorToString(eErr));
      m_EncoderLastError = eErr;
    }

    if(AL_IS_WARNING_CODE(eErr))
      LogWarning("%s\n", AL_Codec_ErrorToString(eErr));

    if(pStream->buf() && shouldAddDummySei)
    {
      constexpr int32_t payloadSize = 8 * 10;
      uint8_t payload[payloadSize];

      for(int32_t i = 0; i < payloadSize; ++i)
        payload[i] = i;

      AL_TStreamMetaData* pStreamMeta = (AL_TStreamMetaData*)AL_Buffer_GetMetaData(pStream->buf(), AL_META_TYPE_STREAM);
      AddSei(pStream->buf(), false, 15, payload, payloadSize, pStreamMeta->uTemporalID);
      AddSei(pStream->buf(), true, 18, payload, payloadSize, pStreamMeta->uTemporalID);
    }

    if(pStream->buf() == EndOfStream)
      iPendingStreamCnt--;
    else
    {
      if(m_pictureType != -1)
      {
        auto const pMeta = (AL_TPictureMetaData*)AL_Buffer_GetMetaData(pStream->buf(), AL_META_TYPE_PICTURE);
        m_pictureType = pMeta->eType;
        LogInfo("Picture Type %s (%i) %s\n", PictTypeToString(pMeta->eType).c_str(), m_pictureType, pMeta->bSkipped ? "is skipped" : "");
      }

      AL_TRateCtrlMetaData* pMeta = (AL_TRateCtrlMetaData*)AL_Buffer_GetMetaData(pStream->buf(), AL_META_TYPE_RATECTRL);

      if(pMeta && pMeta->bFilled)
      {
      }
      std::vector<std::string_view> vec;
      pStream->walkBuffers([&vec](size_t size, uint8_t* data) { vec.push_back({(char*)data, size}); });
      dataCallback_(vec);
    }

    return AL_SUCCESS;
  }

  void CloseOutputs(void)
  {
    m_EndTime = GetPerfTime();

    // Signal that encoding is complete
    {
      std::lock_guard<std::mutex> lock(encoding_complete_mutex);
      encoding_finished = true;
    }
    encoding_complete_cv.notify_all();
  }

  void CheckAndAllocateConversionBuffer(TFourCC tConvFourCC, AL_TDimension const& tConvDim, std::shared_ptr<AL_TBuffer>& pConvYUV)
  {
    if(pConvYUV != nullptr)
    {
      AL_TDimension tCurrentConvDim = AL_PixMapBuffer_GetDimension(pConvYUV.get());

      if(tCurrentConvDim.iHeight >= tConvDim.iHeight && tCurrentConvDim.iWidth >= tConvDim.iWidth)
        return;
    }

    AL_TBuffer* pYuv = AllocateDefaultYuvIOBuffer(tConvDim, tConvFourCC);

    if(pYuv == nullptr)
      throw std::runtime_error("Couldn't allocate reconstruct conversion buffer");

    pConvYUV = std::shared_ptr<AL_TBuffer>(pYuv, &AL_Buffer_Destroy);

  }

  void RecToYuv(AL_TBuffer const* pRec, AL_TBuffer* pYuv, TFourCC tYuvFourCC)
  {
    TFourCC tRecFourCC = AL_PixMapBuffer_GetFourCC(pRec);
    tConvFourCCFunc pFunc = GetConvFourCCFunc(tRecFourCC, tYuvFourCC);

    AL_PixMapBuffer_SetDimension(pYuv, AL_PixMapBuffer_GetDimension(pRec));

    if(!pFunc)
      throw std::runtime_error("Can't find a conversion function suitable for format");

    if(AL_IsTiled(tRecFourCC) == false)
      throw std::runtime_error("FourCC must be in Tile mode");
    return pFunc(pRec, pYuv);
  }

  void processOutput(cv::Ptr<cv::vcucodec::Data> pStream)
  {
    AL_ERR eErr;
    {
      eErr = PreprocessOutput(pStream);
    }

    if(AL_IS_ERROR_CODE(eErr))
    {
      LogError("%s\n", AL_Codec_ErrorToString(eErr));
      m_EncoderLastError = eErr;
    }

    if(AL_IS_WARNING_CODE(eErr))
      LogWarning("%s\n", AL_Codec_ErrorToString(eErr));


    AL_TRecPic RecPic;

    while(AL_Encoder_GetRecPicture(hEnc, &RecPic))
    {
      auto buf = RecPic.pBuf;
      int32_t iRecId = 0;

      if(buf)
      {
        TFourCC tFileRecFourCC = m_cfg.RecFourCC;
        AL_Buffer_InvalidateMemory(buf);

        TFourCC fourCC = AL_PixMapBuffer_GetFourCC(buf);

        if(AL_IsCompressed(fourCC))
          RecOutput[iRecId]->ProcessFrame(buf);
        else
        {
          if(AL_PixMapBuffer_GetFourCC(buf) != tFileRecFourCC)
          {
            std::shared_ptr<AL_TBuffer> bufPostConv;
            CheckAndAllocateConversionBuffer(tFileRecFourCC, AL_PixMapBuffer_GetDimension(buf), bufPostConv);
            RecToYuv(buf, bufPostConv.get(), tFileRecFourCC);
            RecOutput[iRecId]->ProcessFrame(bufPostConv.get());
          }
          else
            RecOutput[iRecId]->ProcessFrame(buf);
        }
      }
      AL_Encoder_ReleaseRecPicture(hEnc, &RecPic);
    }

    if(iPendingStreamCnt == 0)
      CloseOutputs();
  }


  void RequestSourceChange(int32_t iInputIdx, int32_t iLayerIdx)
  {
    if(m_changeSourceCB)
      m_changeSourceCB(iInputIdx, iLayerIdx);
  }

  void CheckSourceResolutionChanged(AL_TBuffer* pSrc)
  {
    (void)pSrc;
    AL_TDimension tNewDim = AL_PixMapBuffer_GetDimension(pSrc);
    bool bDimensionChanged = tNewDim.iWidth != tLastEncodedDim.iWidth || tNewDim.iHeight != tLastEncodedDim.iHeight;

    if(bDimensionChanged)
    {
      AL_Encoder_SetInputResolution(hEnc, tNewDim);
      tLastEncodedDim = tNewDim;
    }
  }
};
