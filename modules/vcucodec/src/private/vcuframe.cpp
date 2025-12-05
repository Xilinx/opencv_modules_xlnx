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
#include "vcuframe.hpp"

#include "opencv2/vcucodec.hpp"
#include "vcuutils.hpp"
#include <opencv2/core/mat.hpp>

extern "C" {
#include "lib_common/BufferAPI.h"
#include "lib_common/BufferPixMapMeta.h"
#include "lib_common/PicFormat.h"
#include "lib_common/DisplayInfoMeta.h"
#include "lib_common/FourCC.h"
#include "lib_common/PixMapBuffer.h"

#include "lib_common_dec/DecInfo.h"
}

#include <functional>
#include <cstring>

namespace cv {
namespace vcucodec {

namespace { // anonymous

void sFreeWithoutDestroyingMemory(AL_TBuffer *buffer)
{
    buffer->iChunkCnt = 0;
    AL_Buffer_Destroy(buffer);
}

void sDestroyFrame(AL_TBuffer *buffer)
{
    AL_Buffer_Destroy(buffer);
}

void copyPlane(const uint8_t* pSrc, uint8_t* pDst, int32_t srcPitch, int32_t dstPitch,
               int32_t width, int32_t height, int32_t bytesPerPixel)
{
    int32_t lineSize = width * bytesPerPixel;
    for (int32_t y = 0; y < height; ++y)
    {
        std::memcpy(pDst, pSrc, lineSize);
        pSrc += srcPitch;
        pDst += dstPitch;
    }
}

void copyToBuffer(std::shared_ptr<AL_TBuffer> buffer,
                  const uint8_t* pSrcY, const uint8_t* pSrcU, const uint8_t* pSrcV,
                  int32_t srcPitchY, int32_t srcPitchU, int32_t srcPitchV,
                  const AL_TDimension& dimension,
                  const AL_TPicFormat& tPicFormat)
{
    if (!buffer)
        throw std::invalid_argument("Buffer must not be null");

    AL_PixMapBuffer_SetDimension(buffer.get(), dimension);

    int fourcc = AL_PixMapBuffer_GetFourCC(buffer.get());

    int32_t bytesPerPixel = tPicFormat.uBitDepth > 8 ? 2 : 1;

    // Get destination buffer addresses and pitches
    uint8_t* pDstY = AL_PixMapBuffer_GetPlaneAddress(buffer.get(), AL_PLANE_Y);
    int32_t dstPitchY = AL_PixMapBuffer_GetPlanePitch(buffer.get(), AL_PLANE_Y);

    // Copy Y plane
    int32_t yHeight = dimension.iHeight;
    int32_t yWidth = dimension.iWidth;

    if (pSrcY)
    {
        copyPlane(pSrcY, pDstY, srcPitchY, dstPitchY, yWidth, yHeight, bytesPerPixel);
    }

    // Handle chroma planes based on format
    if (tPicFormat.eChromaMode != AL_CHROMA_MONO)
    {
        AL_EPlaneMode planeMode = AL_GetPlaneMode(fourcc);

        if (planeMode == AL_PLANE_MODE_SEMIPLANAR)
        {
            // Semi-planar: NV12, P010, NV16 - interleaved UV
            uint8_t* pDstUV = AL_PixMapBuffer_GetPlaneAddress(buffer.get(), AL_PLANE_UV);
            int32_t dstPitchUV = AL_PixMapBuffer_GetPlanePitch(buffer.get(), AL_PLANE_UV);

            int32_t uvHeight, uvWidth;
            if (tPicFormat.eChromaMode == AL_CHROMA_4_2_0)
            {
                uvHeight = (yHeight + 1) / 2;
                uvWidth = yWidth;
            }
            else if (tPicFormat.eChromaMode == AL_CHROMA_4_2_2)
            {
                uvHeight = yHeight;
                uvWidth = yWidth;
            }
            else
            {
                throw std::runtime_error("Unsupported chroma mode for semi-planar");
            }

            if (pSrcU)
            {
                // pSrcU points to interleaved UV data
                copyPlane(pSrcU, pDstUV, srcPitchU, dstPitchUV, uvWidth, uvHeight, bytesPerPixel);
            }
        }
        else
        {
            // Planar: I420, YV12 - separate U and V planes
            uint8_t* pDstU = AL_PixMapBuffer_GetPlaneAddress(buffer.get(), AL_PLANE_U);
            uint8_t* pDstV = AL_PixMapBuffer_GetPlaneAddress(buffer.get(), AL_PLANE_V);
            int32_t dstPitchU = AL_PixMapBuffer_GetPlanePitch(buffer.get(), AL_PLANE_U);
            int32_t dstPitchV = AL_PixMapBuffer_GetPlanePitch(buffer.get(), AL_PLANE_V);

            int32_t uvHeight, uvWidth;
            if (tPicFormat.eChromaMode == AL_CHROMA_4_2_0)
            {
                uvHeight = (yHeight + 1) / 2;
                uvWidth = (yWidth + 1) / 2;
            }
            else if (tPicFormat.eChromaMode == AL_CHROMA_4_2_2)
            {
                uvHeight = yHeight;
                uvWidth = (yWidth + 1) / 2;
            }
            else if (tPicFormat.eChromaMode == AL_CHROMA_4_4_4)
            {
                uvHeight = yHeight;
                uvWidth = yWidth;
            }
            else
            {
                throw std::runtime_error("Unsupported chroma mode for planar");
            }

            if (pSrcU && pSrcV)
            {
                copyPlane(pSrcU, pDstU, srcPitchU, dstPitchU, uvWidth, uvHeight, bytesPerPixel);
                copyPlane(pSrcV, pDstV, srcPitchV, dstPitchV, uvWidth, uvHeight, bytesPerPixel);
            }
        }
    }
}

} // namespace anonymous

Frame::Frame(AL_TBuffer* frame, AL_TInfoDecode const * info, FrameCB cb/* = FrameCB()*/)
    : frame_(frame, [](AL_TBuffer* buf) { if(buf) AL_Buffer_Unref(buf); })
    , info_(new AL_TInfoDecode(*info)), callback_(cb)
{
    AL_Buffer_Ref(frame);
}

Frame::Frame(Frame const &frame) // shallow copy constructor
    : info_(new AL_TInfoDecode(*frame.info_))
{
    AL_TBuffer* shallowCopy = AL_Buffer_ShallowCopy(frame.frame_.get(), &sFreeWithoutDestroyingMemory);
    if (!shallowCopy)
        throw std::runtime_error("Failed to create shallow copy of buffer");

    frame_ = std::shared_ptr<AL_TBuffer>(shallowCopy, [](AL_TBuffer* buf) { if(buf) AL_Buffer_Unref(buf); });

    AL_TMetaData *pMetaD;
    AL_TPixMapMetaData *pPixMeta = (AL_TPixMapMetaData *)AL_Buffer_GetMetaData(
        frame.frame_.get(), AL_META_TYPE_PIXMAP);
    if (!pPixMeta) throw std::runtime_error("PixMapMetaData is NULL");
    AL_TDisplayInfoMetaData *pDispMeta = (AL_TDisplayInfoMetaData *)AL_Buffer_GetMetaData(
        frame.frame_.get(), AL_META_TYPE_DISPLAY_INFO);
    if (!pDispMeta) throw std::runtime_error("DisplayInfoMetaData is NULL");
    pMetaD = (AL_TMetaData *)AL_PixMapMetaData_Clone(pPixMeta);
    if (!pMetaD) throw std::runtime_error("Clone of PixMapMetaData was not created!");
    if (!AL_Buffer_AddMetaData(frame_.get(), pMetaD))
    {
        AL_MetaData_Destroy(pMetaD);
        throw std::runtime_error("Cloned PixMapMetaData did not get added!\n");
    }
    pMetaD = (AL_TMetaData *)AL_DisplayInfoMetaData_Clone(pDispMeta);
    if (!pMetaD) throw std::runtime_error("Clone of DisplayInfoMetaData was not created!");
    if (!AL_Buffer_AddMetaData(frame_.get(), pMetaD))
    {
        AL_MetaData_Destroy(pMetaD);
        throw std::runtime_error("Cloned DisplayInfoMetaData did not get added!\n");
    }
    AL_Buffer_Ref(frame_.get());
}

Frame::Frame(Size const &size, int fourcc)
    : info_(new AL_TInfoDecode())
{
    if (fourcc == FOURCC(NULL))
    {
        throw std::runtime_error("FOURCC cannot be NULL");
    }

    AL_TPicFormat tPicFormat;
    AL_GetPicFormat(fourcc, &tPicFormat);
    AL_TDimension tDim = {size.width, size.height};
    AL_TDimension tRoundedDim = {(size.width + 7) & ~7, (size.height + 7) & ~7};
    AL_TBuffer* rawBuffer = AL_PixMapBuffer_Create_And_AddPlanes(AL_GetDefaultAllocator(), sDestroyFrame, tDim,
                                                   tRoundedDim, tPicFormat, 1, "IO frame buffer");
    if (!rawBuffer)
    {
        throw std::runtime_error("Failed to create buffer");
    }
    frame_ = std::shared_ptr<AL_TBuffer>(rawBuffer, [](AL_TBuffer* buf) { if(buf) AL_Buffer_Unref(buf); });
    AL_Buffer_Ref(frame_.get());
    AL_PixMapBuffer_SetDimension(frame_.get(), tDim);
    info_->tDim = tRoundedDim;
    bool bCropped = tDim.iWidth != tRoundedDim.iWidth || tDim.iHeight != tRoundedDim.iHeight;
    info_->eChromaMode = tPicFormat.eChromaMode;
    info_->uBitDepthY = tPicFormat.uBitDepth;
    info_->uBitDepthC = tPicFormat.uBitDepth;
    uint32_t cropped_width = bCropped ? static_cast<uint32_t>(tDim.iWidth) : 0;
    uint32_t cropped_height = bCropped ? static_cast<uint32_t>(tDim.iHeight) : 0;
    info_->tCrop = {bCropped, 0, 0, cropped_width, cropped_height};
    info_->eFbStorageMode = tPicFormat.eStorageMode;
    info_->ePicStruct = AL_PS_FRM;
    info_->uCRC = 0;
    info_->eOutputID = AL_OUTPUT_MAIN;
    info_->tPos = {0, 0};
}

Frame::Frame(std::shared_ptr<AL_TBuffer> buffer, const Mat& mat, const AL_TDimension& dimension,
              const FormatInfo& formatInfo)
    : frame_(buffer), info_(new AL_TInfoDecode())
{
    if (!buffer)
        throw std::invalid_argument("Frame buffer must not be null");

    const uint8_t* srcData = mat.ptr<uint8_t>();
    if (!srcData)
        throw std::invalid_argument("Input matrix data must not be null");

    int32_t srcPitch = static_cast<int32_t>(mat.step[0]);

    // Use cached format info from encoder
    const AL_TPicFormat& tPicFormat = formatInfo.format;
    int fourcc = formatInfo.fourcc;
    AL_EPlaneMode planeMode = AL_GetPlaneMode(fourcc);

    // Calculate plane pointers based on format
    const uint8_t* pSrcY = srcData;
    const uint8_t* pSrcU = nullptr;
    const uint8_t* pSrcV = nullptr;

    if (tPicFormat.eChromaMode != AL_CHROMA_MONO)
    {
        // UV plane starts after Y plane
        pSrcU = srcData + (dimension.iHeight * srcPitch);

        if (planeMode == AL_PLANE_MODE_PLANAR && tPicFormat.eChromaMode != AL_CHROMA_MONO)
        {
            // For planar formats, V plane follows U plane
            int32_t uvHeight;
            if (tPicFormat.eChromaMode == AL_CHROMA_4_2_0)
                uvHeight = (dimension.iHeight + 1) / 2;
            else if (tPicFormat.eChromaMode == AL_CHROMA_4_2_2)
                uvHeight = dimension.iHeight;
            else // AL_CHROMA_4_4_4
                uvHeight = dimension.iHeight;

            pSrcV = pSrcU + (uvHeight * srcPitch);
        }
    }

    copyToBuffer(buffer, pSrcY, pSrcU, pSrcV, srcPitch, srcPitch, srcPitch, dimension, tPicFormat);
}

Frame::~Frame()
{
    if (frame_)
    {
        if (callback_)
        {
            callback_(*this);
        }
        // shared_ptr will handle AL_Buffer_Unref automatically
        frame_.reset();
    }
}

void Frame::invalidate()
{
    if (frame_)
    {
        AL_Buffer_InvalidateMemory(frame_.get());
    }
}

void Frame::rawInfo(RawInfo& rawInfo) const {
    AL_TBuffer* pFrame = getBuffer();
    TFourCC fourcc = AL_PixMapBuffer_GetFourCC(pFrame);
    AL_EPlaneMode planeMode = AL_GetPlaneMode(fourcc);

    AL_TDimension tYuvDim = AL_PixMapBuffer_GetDimension(pFrame);
    //int32_t bitdepth = AL_GetBitDepth(fourcc);
    int32_t stride = AL_PixMapBuffer_GetPlanePitch(pFrame, AL_PLANE_Y);
    int32_t strideChroma = AL_PixMapBuffer_GetPlanePitch(pFrame,
        (AL_PLANE_MODE_SEMIPLANAR == planeMode) ? AL_PLANE_UV : AL_PLANE_U);
    AL_TCropInfo cropInfo = getCropInfo();
    bool cropping = cropInfo.bCropping;

    // frame_info.eos : set elsewhere
    rawInfo.fourcc = fourcc;
    rawInfo.picStruct = static_cast<PicStruct>(info_->ePicStruct);
    rawInfo.bitsPerLuma = bitDepthY();
    rawInfo.bitsPerChroma = bitDepthUV();
    rawInfo.stride = stride;
    rawInfo.strideChroma = strideChroma;
    rawInfo.width = tYuvDim.iWidth;
    rawInfo.height = tYuvDim.iHeight;
    rawInfo.posX = 0;
    rawInfo.posY = 0;
    rawInfo.cropTop = cropping? cropInfo.uCropOffsetTop : 0;
    rawInfo.cropBottom = cropping? cropInfo.uCropOffsetBottom : 0;
    rawInfo.cropLeft = cropping? cropInfo.uCropOffsetLeft : 0;
    rawInfo.cropRight = cropping? cropInfo.uCropOffsetRight : 0;
}


AL_TBuffer *Frame::getBuffer() const { return frame_.get(); }

std::shared_ptr<AL_TBuffer> Frame::getSharedBuffer() const { return frame_; }

AL_TInfoDecode const &Frame::getInfo() const { return *info_; }

bool Frame::isMainOutput() const
{
    return (info_->eOutputID == AL_OUTPUT_MAIN || info_->eOutputID == AL_OUTPUT_POSTPROC);
}

unsigned int Frame::bitDepthY() const
{
    return info_->uBitDepthY;
}

unsigned int Frame::bitDepthUV() const
{
    return info_->uBitDepthC;
}

AL_TCropInfo const &Frame::getCropInfo() const
{
    return info_->tCrop;
}

AL_TDimension const &Frame::getDimension() const
{
    return info_->tDim;
}

int Frame::getFourCC() const
{
    return AL_PixMapBuffer_GetFourCC(frame_.get());
}

/*static*/ Ptr<Frame>
Frame::create(AL_TBuffer *pFrame, AL_TInfoDecode const *pInfo, FrameCB cb /*= FrameCB()*/)
{
    return Ptr<Frame>(new Frame(pFrame, pInfo, cb));
}

/*static*/ Ptr<Frame> Frame::createYuvIO(Size const &size, int fourcc)
{
    return Ptr<Frame>(new Frame(size, fourcc));
}

/*static*/ Ptr<Frame> Frame::createShallowCopy(Ptr<Frame> const &frame)
{
    if (!frame || !frame->getBuffer())
    {
        return Ptr<Frame>();
    }
    return Ptr<Frame>(new Frame(*frame));
}

/*static*/ Ptr<Frame> Frame::createFromMat(std::shared_ptr<AL_TBuffer> buffer,
                                           const Mat& mat,
                                           const AL_TDimension& dimension,
                                           const FormatInfo& formatInfo)
{
    return Ptr<Frame>(new Frame(buffer, mat, dimension, formatInfo));
}

/// Link the life cycle of this frame to another frame.
void Frame::link(Ptr<Frame> frame)
{
    linkedFrame_ = frame;
}

// class  FrameQueue

FrameQueue::FrameQueue() {}

FrameQueue::~FrameQueue() {}

void FrameQueue::enqueue(Ptr<Frame> frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(frame);
    cv_.notify_one();
}

Ptr<Frame> FrameQueue::dequeue(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (cv_.wait_for(lock, timeout, [this]{ return !queue_.empty(); }))
    {
        Ptr<Frame> frame = queue_.front();
        queue_.pop();
        return frame;
    }
    return nullptr;
}

bool FrameQueue::empty()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

void FrameQueue::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty())
    {
        queue_.pop();
    }
}


} // namespace vcucodec
} // namespace cv
