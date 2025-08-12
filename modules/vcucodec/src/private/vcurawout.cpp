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
#include "vcurawout.hpp"

extern "C" {
#include "config.h"
#include "lib_decode/lib_decode.h"
#include "lib_common/FourCC.h"
#include "lib_common/DisplayInfoMeta.h"
#include "lib_common/PixMapBuffer.h"
}
#include "lib_app/convert.h"


#include <iostream>

namespace cv {
namespace vcucodec {

namespace { // anonymous

const int32_t OUTPUT_BD_FIRST = 0;
const int32_t OUTPUT_BD_ALLOC = -1;
const int32_t OUTPUT_BD_STREAM = -2;

int32_t convertBitDepthToEven(int32_t iBd)
{
    return ((iBd % 2) != 0) ? iBd + 1 : iBd;
}


} // anonymous namespace


class RawOutputImpl : public RawOutput
{
public:
    ~RawOutputImpl() override = default;
    void configure(int fourcc, unsigned int bitDepth, int max_frames) override;

    bool process(Ptr<Frame> frame, int32_t iBitDepthAlloc,
                 bool& bIsMainDisplay, bool& bNumFrameReached, bool bDecoderExists) override;
    Ptr<Frame> dequeue(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) override;
    bool idle() override;
    void flush() override;

private:
    void processFrame(Ptr<Frame>, int32_t iBdOut, TFourCC tOutFourCC);

    void copyMetaData(AL_TBuffer* pDstFrame, AL_TBuffer* pSrcFrame, AL_EMetaType eMetaType);

    Ptr<Frame> convertFrameBuffer(Ptr<Frame> frame, int32_t iBdOut, AL_TPosition const& tPos,
                                  TFourCC tOutFourCC);

    AL_EFbStorageMode eMainOutputStorageMode;
    bool bOutputWritersCreated = false;
    int32_t iBitDepth = 8;
    uint32_t uNumFrames = 0;
    uint32_t uMaxFrames = UINT32_MAX;
    TFourCC tOutputFourCC = FOURCC(NULL);
    TFourCC tInputFourCC = FOURCC(NULL);

    bool bHasOutput = false;
    bool bEnableYuvOutput = false;
    FrameQueue frame_queue_;
};


void RawOutputImpl::configure(int fourcc, unsigned int bitDepth, int max_frames)
{
    tOutputFourCC = fourcc;
    if(tOutputFourCC != FOURCC(NULL)) {
        eMainOutputStorageMode = AL_GetStorageMode(tOutputFourCC);
    }
    else
    {
     eMainOutputStorageMode = AL_FB_RASTER;
    }

    bHasOutput = true;
    bEnableYuvOutput = false;

    iBitDepth = bitDepth;
    uMaxFrames = max_frames;
}


void RawOutputImpl::copyMetaData(AL_TBuffer* pDstFrame, AL_TBuffer* pSrcFrame, AL_EMetaType eMetaType)
{
    AL_TMetaData* pMetaD = nullptr;

    AL_TMetaData* pOrigMeta = AL_Buffer_GetMetaData(pSrcFrame, eMetaType);

    if(!pOrigMeta)
        throw std::runtime_error("Metadata is NULL");
    switch(eMetaType)
    {
    case AL_META_TYPE_PIXMAP:
        pMetaD = (AL_TMetaData*)AL_PixMapMetaData_Clone((AL_TPixMapMetaData*)pOrigMeta);
        break;

    case AL_META_TYPE_DISPLAY_INFO:
        pMetaD = (AL_TMetaData*)AL_DisplayInfoMetaData_Clone((AL_TDisplayInfoMetaData*)pOrigMeta);
        break;
    default:
        throw std::runtime_error("Metadata type is not supported");
        break;
    }

    if(pMetaD == NULL)
        throw std::runtime_error("Clone of MetaData was not created!");

    if(!AL_Buffer_AddMetaData(pDstFrame, pMetaD))
        throw std::runtime_error("Cloned pMetaD did not get added!\n");
}

bool RawOutputImpl::process(Ptr<Frame> frame, int32_t iBitDepthAlloc, bool& bIsMainDisplay,
                             bool& bNumFrameReached, bool bDecoderExists)
{
    AL_TBuffer* pFrame = frame->getBuffer();
    bNumFrameReached = false;
    bIsMainDisplay = frame->isMainOutput();

    if (bDecoderExists)
    {
        if (uNumFrames < uMaxFrames)
        {
            if(!AL_Buffer_GetData(pFrame))
                throw std::runtime_error("Data buffer is null");

            Ptr<Frame> pDFrame = Frame::createShallowCopy(frame);
            pDFrame->link(frame); // Link the life cycle of the original frame to the shallow copy

            int32_t iCurrentBitDepth = max(frame->bitDepthY(), frame->bitDepthUV());

            if (iBitDepth == OUTPUT_BD_FIRST)
                iBitDepth = iCurrentBitDepth;
            else if(iBitDepth == OUTPUT_BD_ALLOC)
                iBitDepth = iBitDepthAlloc;

            int32_t iEffectiveBitDepth = iBitDepth == OUTPUT_BD_STREAM ? iCurrentBitDepth : iBitDepth;

            if (bHasOutput)
                processFrame(pDFrame, iEffectiveBitDepth, tOutputFourCC);

            if (bIsMainDisplay)
            {
                // TODO: increase only when last frame
                //DisplayFrameStatus(uNumFrames);
            }
        }

        if(bIsMainDisplay)
            uNumFrames++;
    }

    if (uNumFrames >= uMaxFrames)
        bNumFrameReached = true;

    return bNumFrameReached;
}

Ptr<Frame> RawOutputImpl::convertFrameBuffer(Ptr<Frame> frame, int32_t iBdOut,
    AL_TPosition const& tPos, TFourCC tOutFourCC)
{
    (void) iBdOut;
    AL_TBuffer* pInput = frame->getBuffer();
    //TFourCC tRecFourCC = AL_PixMapBuffer_GetFourCC(pInput);
    AL_TDimension tRecDim = AL_PixMapBuffer_GetDimension(pInput);
    //AL_EChromaMode eRecChromaMode = AL_GetChromaMode(tRecFourCC);

    TFourCC tConvFourCC = tOutFourCC;
    //AL_TPicFormat tConvPicFormat;
    assert(tConvFourCC);

    AL_PixMapBuffer_SetDimension(pInput, { tPos.iX + tRecDim.iWidth, tPos.iY + tRecDim.iHeight });

    AL_TDimension tDim = AL_PixMapBuffer_GetDimension(pInput);
    Ptr<Frame> pFrame = Frame::createYuvIO({tDim.iWidth, tDim.iHeight}, tConvFourCC);
    AL_TBuffer* pOutput = pFrame->getBuffer();

    if(ConvertPixMapBuffer(pInput, pOutput))
        throw std::runtime_error("Couldn't convert buffer");

    AL_PixMapBuffer_SetDimension(pInput, tRecDim);
    copyMetaData(pOutput, pInput, AL_META_TYPE_DISPLAY_INFO);
    return pFrame;
}

Ptr<Frame> RawOutputImpl::dequeue(std::chrono::milliseconds timeout)
{
    return frame_queue_.dequeue(timeout);
}

bool RawOutputImpl::idle() {
    return frame_queue_.empty();
}

void RawOutputImpl::flush() {
    frame_queue_.clear();
}

void RawOutputImpl::processFrame(Ptr<Frame> frame, int32_t iBdOut, TFourCC tOutFourCC)
{
    AL_TBuffer& tRecBuf = *frame->getBuffer();
    AL_PixMapBuffer_SetDimension(&tRecBuf, frame->getDimension());

    iBdOut = convertBitDepthToEven(iBdOut);

    AL_TCropInfo tCrop {};
    tCrop = frame->getCropInfo();
    AL_TPosition tPos = { 0, 0 };

    TFourCC tRecBufFourCC = frame->getFourCC();
    AL_TPicFormat tRecPicFormat;
    AL_GetPicFormat(tRecBufFourCC, &tRecPicFormat);

    bool bNewInputFourCCFound = false;

    if (tInputFourCC != tRecBufFourCC)
    {
        bNewInputFourCCFound = true;
        tInputFourCC = tRecBufFourCC;
    }

    if (tOutFourCC == FOURCC(NULL))
    {
        AL_EPlaneMode ePlaneMode = AL_PLANE_MODE_PLANAR;

        if (tRecPicFormat.bMSB && (tRecPicFormat.eChromaMode == AL_CHROMA_4_2_0
                                   || tRecPicFormat.eChromaMode == AL_CHROMA_4_2_2))
        ePlaneMode = AL_PLANE_MODE_SEMIPLANAR;

        AL_TPicFormat tConvPicFormat = AL_TPicFormat {
            tRecPicFormat.eChromaMode,
            AL_ALPHA_MODE_DISABLED,
            static_cast<uint8_t>(iBdOut),
            AL_FB_RASTER,
            ePlaneMode,
            AL_COMPONENT_ORDER_YUV,
            AL_SAMPLE_PACK_MODE_BYTE,
            false,
            tRecPicFormat.bMSB
        };

        tOutFourCC = AL_GetFourCC(tConvPicFormat);
    }
    else if(tOutFourCC == FOURCC(hard))
    {
        tOutFourCC = tRecBufFourCC;
    }

    bool bCompress = AL_IsCompressed(tRecBufFourCC);
    bool bConvert = !bCompress && tOutFourCC != tRecBufFourCC;

    AL_TDisplayInfoMetaData* pMeta = reinterpret_cast<AL_TDisplayInfoMetaData*>(
        AL_Buffer_GetMetaData(&tRecBuf, AL_META_TYPE_DISPLAY_INFO));

    if (pMeta)
        pMeta->tCrop = tCrop;

    if (bConvert)
    {
        if (tInputFourCC != tOutFourCC && bNewInputFourCCFound)
        {
            std::cout << "Software conversion done from " << AL_FourCCToString(tRecBufFourCC).cFourcc
                      << " to " << AL_FourCCToString(tOutFourCC).cFourcc << std::endl;
        }
        Ptr<Frame> pYuvFrame = convertFrameBuffer(frame, iBdOut, tPos, tOutFourCC);

        frame_queue_.enqueue(pYuvFrame);
    }
    else
    {
        frame_queue_.enqueue(frame);
    }
}

Ptr<RawOutput> RawOutput::create()
{
    return Ptr<RawOutput>(new RawOutputImpl());
}

} // namespace vcucodec
} // namespace cv