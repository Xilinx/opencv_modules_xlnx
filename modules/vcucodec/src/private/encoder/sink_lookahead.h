// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <stdexcept>
#include <cassert>
#include "IEncoderSink.hpp"
#include "TwoPassMngr.h"

extern "C"
{
#include "lib_common/BufferLookAheadMeta.h"
}


#include "../vcuutils.hpp"
#include "../vcuenccontext.hpp"

using codec_error = cv::vcucodec::en_codec_error;

/*
** Special EncoderSink structure, used for encoding the first pass
** The encoding settings are adapted for the first pass
** LookAhead Metadata is created and filled during the encoding
** Frames are stocked in a fifo after the first pass
** More information are filled in the metadata while in the fifo (scene change, complexity)
** The frames are then sent to the real EncoderSink for the second pass, with first pass info in the metadata
*/

struct EncoderLookAheadSink : IEncoderSink
{
#ifdef HAVE_VCU2_CTRLSW
  explicit EncoderLookAheadSink(IEncoderSink* pNext
                                , cv::vcucodec::EncContext::Config const& cfg
                                , AL_RiscV_Ctx ctx
                                , AL_TAllocator* pAllocator) :
    hEnc(nullptr),
    m_pNext(pNext),
    m_lookAheadMngr(cfg.Settings.LookAhead, cfg.Settings.bEnableFirstPassSceneChangeDetection),
    tLastEncodedDim{cfg.Settings.tChParam[0].uSrcWidth, cfg.Settings.tChParam[0].uSrcHeight}
  {
    assert(ctx);
    m_pBitstreamOutput.reset(new NullFrameSink);
    m_pRecOutput.reset(new NullFrameSink);

    AL_CB_EndEncoding onEndEncoding = { &EncoderLookAheadSink::EndEncoding, this };
    m_cfgLA = cfg;
    AL_TwoPassMngr_SetPass1Settings(m_cfgLA.Settings);

    if(AL_Settings_CheckCoherency(&m_cfgLA.Settings, &m_cfgLA.Settings.tChParam[0], m_cfgLA.MainInput.FileInfo.FourCC, NULL) < 0)
      throw std::runtime_error("Incoherent settings!");

    m_qpBuffers.Configure(&m_cfgLA.Settings, m_cfgLA.RunInfo.eGenerateQpMode);

    AL_ERR errorCode = AL_Encoder_CreateWithCtx(&hEnc, ctx, pAllocator, &m_cfgLA.Settings, onEndEncoding);

    if(errorCode)
      throw codec_error(AL_Codec_ErrorToString(errorCode), errorCode);

    m_iPictureType = cfg.RunInfo.printPictureType ? AL_SLICE_MAX_ENUM : -1;

    m_bEnableFirstPassSceneChangeDetection = false;
    m_bEnableFirstPassSceneChangeDetection = cfg.Settings.bEnableFirstPassSceneChangeDetection;
    m_EOSFinished = Rtos_CreateEvent(false);
    m_FifoFlushFinished = Rtos_CreateEvent(false);
    m_iNumLayer = cfg.Settings.NumLayer;

    m_iNumFrameEnded = 0;

    m_EncoderLastError = AL_SUCCESS;
    m_iMaxpicCount = cfg.RunInfo.iMaxPict;
  }
#endif

  explicit EncoderLookAheadSink(IEncoderSink* pNext
                                , cv::vcucodec::EncContext::Config const& cfg
                                , AL_IEncScheduler* pScheduler
                                , AL_TAllocator* pAllocator) :
    hEnc(nullptr),
    m_pNext(pNext),
    m_lookAheadMngr(cfg.Settings.LookAhead, cfg.Settings.bEnableFirstPassSceneChangeDetection),
    tLastEncodedDim{cfg.Settings.tChParam[0].uSrcWidth, cfg.Settings.tChParam[0].uSrcHeight}
  {
    m_pBitstreamOutput.reset(new NullFrameSink);
    m_pRecOutput.reset(new NullFrameSink);

    AL_CB_EndEncoding onEndEncoding = { &EncoderLookAheadSink::EndEncoding, this };
    m_cfgLA = cfg;
    AL_TwoPassMngr_SetPass1Settings(m_cfgLA.Settings);

    if(AL_Settings_CheckCoherency(&m_cfgLA.Settings, &m_cfgLA.Settings.tChParam[0], m_cfgLA.MainInput.FileInfo.FourCC, NULL) < 0)
      throw std::runtime_error("Incoherent settings!");

    m_qpBuffers.Configure(&m_cfgLA.Settings, m_cfgLA.RunInfo.eGenerateQpMode);

    AL_ERR errorCode = AL_Encoder_Create(&hEnc, pScheduler, pAllocator, &m_cfgLA.Settings, onEndEncoding);

    if(errorCode)
      throw codec_error(AL_Codec_ErrorToString(errorCode), errorCode);

    m_iPictureType = cfg.RunInfo.printPictureType ? AL_SLICE_MAX_ENUM : -1;

    m_bEnableFirstPassSceneChangeDetection = cfg.Settings.bEnableFirstPassSceneChangeDetection;
    m_EOSFinished = Rtos_CreateEvent(false);
    m_FifoFlushFinished = Rtos_CreateEvent(false);
    m_iNumLayer = cfg.Settings.NumLayer;

    m_iNumFrameEnded = 0;

    m_iMaxpicCount = cfg.RunInfo.iMaxPict;
    m_EncoderLastError = AL_SUCCESS;
  }

  ~EncoderLookAheadSink(void)
  {
    AL_Encoder_Destroy(hEnc);
    Rtos_DeleteEvent(m_EOSFinished);
    Rtos_DeleteEvent(m_FifoFlushFinished);
  }

  void SetChangeSourceCallback(ChangeSourceCallback changeSourceCB) override
  {
    m_changeSourceCB = changeSourceCB;
  }

  void AddQpBufPool(QPBuffers::QPLayerInfo qpInf, int32_t iLayerID)
  {
    m_qpBuffers.AddBufPool(qpInf, iLayerID);
  }

  void PreprocessFrame() override
  {

  }

  void ProcessFrame(AL_TBuffer* Src) override
  {
    AL_TBuffer* QpBuf = nullptr;

    if(Src)
    {
      CheckSourceResolutionChanged(Src);

      auto pPictureMetaLA = (AL_TLookAheadMetaData*)AL_Buffer_GetMetaData(Src, AL_META_TYPE_LOOKAHEAD);

      if(!pPictureMetaLA)
      {
        pPictureMetaLA = AL_LookAheadMetaData_Create();

        if(AL_Buffer_AddMetaData(Src, (AL_TMetaData*)pPictureMetaLA) == false)
          throw std::runtime_error("Add metadata shouldn't fail!");
      }
      AL_LookAheadMetaData_Reset(pPictureMetaLA);

      QpBuf = m_qpBuffers.getBuffer(m_iPicCount);
    }

    std::shared_ptr<AL_TBuffer> QpBufShared(QpBuf, [&](AL_TBuffer* pBuf) { m_qpBuffers.releaseBuffer(pBuf); });

    if(m_iPicCount <= m_iMaxpicCount)
    {
      AL_TBuffer* pSrc = (m_iPicCount == m_iMaxpicCount) ? nullptr : Src;

      if(!AL_Encoder_Process(hEnc, pSrc, QpBuf))
        throw std::runtime_error("Failed LA");
    }

    if(Src)
      m_iPicCount++;
    else if(m_iNumLayer == 1)
    {
      // the main process waits for the LookAhead to end so he can flush the fifo
      Rtos_WaitEvent(m_EOSFinished, AL_WAIT_FOREVER);
      ProcessFifo(true, false);
    }
  }

  AL_ERR GetLastError(void) override
  {
    return m_EncoderLastError;
  }

  AL_HEncoder hEnc;

private:
  IEncoderSink* m_pNext;
  std::unique_ptr<IFrameSink> m_pBitstreamOutput;
  std::unique_ptr<IFrameSink> m_pRecOutput;

  int32_t m_iPicCount = 0;
  int32_t m_iMaxpicCount = -1;
  int32_t m_iPictureType = -1;
  cv::vcucodec::EncContext::Config m_cfgLA;
  QPBuffers m_qpBuffers;
  LookAheadMngr m_lookAheadMngr;
  bool m_bEnableFirstPassSceneChangeDetection;
  AL_EVENT m_EOSFinished;
  AL_EVENT m_FifoFlushFinished;
  int32_t m_iNumLayer;
  int32_t m_iNumFrameEnded;

  ChangeSourceCallback m_changeSourceCB;
  AL_TDimension tLastEncodedDim;
  AL_ERR m_EncoderLastError;

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
    auto pThis = (EncoderLookAheadSink*)userParam;

    if(isStreamReleased(pStream, pSrc) || isSourceReleased(pStream, pSrc))
      return;

    pThis->AddFifo(const_cast<AL_TBuffer*>(pSrc), pStream);

    pThis->processOutputLookAhead(pStream);
  }

  void processOutputLookAhead(AL_TBuffer* pStream)
  {
    m_pBitstreamOutput->ProcessFrame(pStream);
    AL_ERR eErr = AL_Encoder_GetLastError(hEnc);

    if(AL_IS_ERROR_CODE(eErr))
    {
      LogError("%s\n", AL_Codec_ErrorToString(eErr));
      m_EncoderLastError = eErr;
    }

    if(AL_IS_WARNING_CODE(eErr))
      LogWarning("%s\n", AL_Codec_ErrorToString(eErr));

    if(pStream)
    {
      if(AL_Encoder_PutStreamBuffer(hEnc, pStream) == false)
        throw std::runtime_error("PutStreamBuffer shouldn't fail");
    }

    AL_TRecPic RecPic;

    while(AL_Encoder_GetRecPicture(hEnc, &RecPic))
    {
      m_pRecOutput->ProcessFrame(RecPic.pBuf);
      AL_Encoder_ReleaseRecPicture(hEnc, &RecPic);
    }
  }

  void AddFifo(AL_TBuffer* pSrc, AL_TBuffer* pStream)
  {
    if(!pSrc)
    {
      Rtos_SetEvent(m_EOSFinished);
      return;
    }

    if(!pStream)
      return;

    bool bIsRepeat = false;
    AL_TPictureMetaData* pPictureMeta = (AL_TPictureMetaData*)AL_Buffer_GetMetaData(pStream, AL_META_TYPE_PICTURE);

    if(pPictureMeta == nullptr)
      throw std::runtime_error("pPictureMeta buffer must exist");
    bIsRepeat = pPictureMeta->eType == AL_SLICE_REPEAT;

    if(bIsRepeat)
      return;

    AL_Buffer_Ref(pSrc);
    m_lookAheadMngr.m_fifo.push_back(pSrc);

    ProcessFifo(false, false);

    ++m_iNumFrameEnded;
  }

  AL_TBuffer* GetSrcBuffer(void)
  {
    AL_TBuffer* pSrc = m_lookAheadMngr.m_fifo.front();
    m_lookAheadMngr.m_fifo.pop_front();
    return pSrc;
  }

  void ProcessFifo(bool isEOS, bool NoFirstPass)
  {
    auto iLASize = m_lookAheadMngr.uLookAheadSize;

    // Fifo is empty, we propagate the EndOfStream
    if(isEOS && m_lookAheadMngr.m_fifo.size() == 0)
    {
      m_pNext->PreprocessFrame();
      m_pNext->ProcessFrame(NULL);
    }
    // Fifo is full, or fifo must be emptied at EOS
    else if((m_lookAheadMngr.m_fifo.size() != 0) && (isEOS || m_iNumFrameEnded == iLASize || NoFirstPass))
    {
      m_iNumFrameEnded--;
      m_lookAheadMngr.ProcessLookAheadParams();
      AL_TBuffer* pSrc = GetSrcBuffer();

      m_pNext->PreprocessFrame();
      m_pNext->ProcessFrame(pSrc);
      AL_Buffer_Unref(pSrc);

      if(isEOS)
        ProcessFifo(isEOS, NoFirstPass);
    }
  }

  void RequestSourceChange(int32_t iInputIdx)
  {
    if(m_changeSourceCB)
      m_changeSourceCB(iInputIdx, 0);
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
