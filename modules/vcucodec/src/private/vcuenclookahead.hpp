#pragma once

#include <deque>
#include <stdexcept>

#include "TwoPassMngr.h"

// First-pass LookAhead encoder sink. Encodes each source frame with pass-1 settings to
// gather look-ahead metadata (scene change, complexity, intra ratio), keeps the source
// frames in a FIFO, then forwards them - metadata attached - to the second-pass
// EncoderSink referenced by next. Mirrors the reference exe_encoder EncoderLookAheadSink.
struct EncoderLookAheadSink
{
#ifdef HAVE_VCU2_CTRLSW
    EncoderLookAheadSink(EncContext::Config const& cfg, AL_RiscV_Ctx ctx, AL_TAllocator* pAllocator)
        : lookAheadMngr(cfg.Settings.LookAhead, cfg.Settings.bEnableFirstPassSceneChangeDetection)
    {
        m_Settings = cfg.Settings;
        AL_TwoPassMngr_SetPass1Settings(m_Settings);
        AL_Settings_CheckCoherency(&m_Settings, &m_Settings.tChParam[0], cfg.MainInput.FileInfo.FourCC, NULL);
        AL_CB_EndEncoding cb = { &EncoderLookAheadSink::EndEncoding, this };
        AL_ERR err = AL_Encoder_CreateWithCtx(&hEnc, ctx, pAllocator, &m_Settings, cb);
        if (AL_IS_ERROR_CODE(err))
            throw en_codec_error(AL_Codec_ErrorToString(err), err);
        EOSFinished = Rtos_CreateEvent(false);
    }
#endif

    EncoderLookAheadSink(EncContext::Config const& cfg, AL_IEncScheduler* pScheduler, AL_TAllocator* pAllocator)
        : lookAheadMngr(cfg.Settings.LookAhead, cfg.Settings.bEnableFirstPassSceneChangeDetection)
    {
        m_Settings = cfg.Settings;
        AL_TwoPassMngr_SetPass1Settings(m_Settings);
        AL_Settings_CheckCoherency(&m_Settings, &m_Settings.tChParam[0], cfg.MainInput.FileInfo.FourCC, NULL);
        AL_CB_EndEncoding cb = { &EncoderLookAheadSink::EndEncoding, this };
        AL_ERR err = AL_Encoder_Create(&hEnc, pScheduler, pAllocator, &m_Settings, cb);
        if (AL_IS_ERROR_CODE(err))
            throw en_codec_error(AL_Codec_ErrorToString(err), err);
        EOSFinished = Rtos_CreateEvent(false);
    }

    ~EncoderLookAheadSink(void)
    {
        // Release any source buffers still queued in the FIFO before the pools are gone.
        while (lookAheadMngr.m_fifo.size() > 0)
        {
            AL_Buffer_Unref(lookAheadMngr.m_fifo.front());
            lookAheadMngr.m_fifo.pop_front();
        }
        AL_Encoder_Destroy(hEnc);
        Rtos_DeleteEvent(EOSFinished);
    }

    void PreprocessFrame(void) {}

    void ProcessFrame(AL_TBuffer* Src)
    {
        if (Src)
        {
            auto pMetaLA = (AL_TLookAheadMetaData*)AL_Buffer_GetMetaData(Src, AL_META_TYPE_LOOKAHEAD);
            if (pMetaLA == NULL)
            {
                pMetaLA = AL_LookAheadMetaData_Create();
                if (AL_Buffer_AddMetaData(Src, (AL_TMetaData*)pMetaLA) == false)
                    throw std::runtime_error("Failed to add LookAhead metadata");
            }
            AL_LookAheadMetaData_Reset(pMetaLA);
            if (AL_Encoder_Process(hEnc, Src, NULL) == false)
                throw std::runtime_error("Failed LookAhead first pass");
        }
        else
        {
            if (AL_Encoder_Process(hEnc, NULL, NULL) == false)
                throw std::runtime_error("Failed LookAhead flush");
            Rtos_WaitEvent(EOSFinished, AL_WAIT_FOREVER);
            ProcessFifo(true);
        }
    }

    AL_HEncoder hEnc;
    EncoderSink* next = NULL;

private:
    AL_TEncSettings m_Settings;
    LookAheadMngr lookAheadMngr;
    AL_EVENT EOSFinished;
    int iNumFrameEnded = 0;

    static bool isStreamReleased(AL_TBuffer* pStream, AL_TBuffer const* pSrc) { return pStream && (pSrc == NULL); }
    static bool isSourceReleased(AL_TBuffer* pStream, AL_TBuffer const* pSrc) { return (pStream == NULL) && pSrc; }

    static void EndEncoding(void* userParam, AL_TBuffer* pStream, AL_TBuffer const* pSrc, int)
    {
        auto pThis = (EncoderLookAheadSink*)userParam;
        if (isStreamReleased(pStream, pSrc) || isSourceReleased(pStream, pSrc))
            return;
        pThis->AddFifo(const_cast<AL_TBuffer*>(pSrc), pStream);
        pThis->recycle(pStream);
    }

    void recycle(AL_TBuffer* pStream)
    {
        if (pStream)
        {
            if (AL_Encoder_PutStreamBuffer(hEnc, pStream) == false)
                throw std::runtime_error("PutStreamBuffer failed (LookAhead)");
        }
        AL_TRecPic RecPic;
        while (AL_Encoder_GetRecPicture(hEnc, &RecPic))
            AL_Encoder_ReleaseRecPicture(hEnc, &RecPic);
    }

    void AddFifo(AL_TBuffer* pSrc, AL_TBuffer* pStream)
    {
        if (pSrc == NULL)
        {
            Rtos_SetEvent(EOSFinished);
            return;
        }
        if (pStream == NULL)
            return;
        auto pPicMeta = (AL_TPictureMetaData*)AL_Buffer_GetMetaData(pStream, AL_META_TYPE_PICTURE);
        if (pPicMeta && pPicMeta->eType == AL_SLICE_REPEAT)
            return;
        AL_Buffer_Ref(pSrc);
        lookAheadMngr.m_fifo.push_back(pSrc);
        ProcessFifo(false);
        ++iNumFrameEnded;
    }

    void ProcessFifo(bool isEOS)
    {
        int iLASize = lookAheadMngr.uLookAheadSize;
        if (isEOS && lookAheadMngr.m_fifo.size() == 0)
        {
            next->PreprocessFrame();
            next->ProcessFrame(NULL);
        }
        else if ((lookAheadMngr.m_fifo.size() > 0) && (isEOS || iNumFrameEnded == iLASize))
        {
            iNumFrameEnded--;
            lookAheadMngr.ProcessLookAheadParams();
            AL_TBuffer* pSrc = lookAheadMngr.m_fifo.front();
            lookAheadMngr.m_fifo.pop_front();
            next->PreprocessFrame();
            next->ProcessFrame(pSrc);
            AL_Buffer_Unref(pSrc);
            if (isEOS)
                ProcessFifo(isEOS);
        }
    }
};
