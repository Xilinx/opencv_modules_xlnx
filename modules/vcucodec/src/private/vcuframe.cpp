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

extern "C" {
#include "lib_common/BufferAPI.h"
#include "lib_common/BufferPixMapMeta.h"
#include "lib_common/PicFormat.h"
#include "lib_common/DisplayInfoMeta.h"
#include "lib_common/FourCC.h"
#include "lib_common/PixMapBuffer.h"

#include "lib_common_dec/DecInfo.h"
}

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

} // namespace anonymous

Frame::Frame(AL_TBuffer* frame, AL_TInfoDecode const * info, FrameCB cb/* = FrameCB()*/)
    : frame_(frame), info_(new AL_TInfoDecode(*info)), callback_(cb)
{
    AL_Buffer_Ref(frame_);
}

Frame::Frame(Frame const &frame) // shallow copy constructor
    : info_(new AL_TInfoDecode(*frame.info_))
{
    frame_ = AL_Buffer_ShallowCopy(frame.frame_, &sFreeWithoutDestroyingMemory);
    AL_Buffer_Ref(frame_);
    AL_TMetaData *pMetaD;
    AL_TPixMapMetaData *pPixMeta = (AL_TPixMapMetaData *)AL_Buffer_GetMetaData(
        frame.frame_, AL_META_TYPE_PIXMAP);
    if (!pPixMeta)
        throw std::runtime_error("PixMapMetaData is NULL");
    AL_TDisplayInfoMetaData *pDispMeta = (AL_TDisplayInfoMetaData *)AL_Buffer_GetMetaData(
        frame.frame_, AL_META_TYPE_DISPLAY_INFO);
    if (!pDispMeta)
        throw std::runtime_error("PixMapMetaData is NULL");
    pMetaD = (AL_TMetaData *)AL_PixMapMetaData_Clone(pPixMeta);
    if (!pMetaD)
        throw std::runtime_error("Clone of PixMapMetaData was not created!");
    AL_Buffer_AddMetaData(frame_, pMetaD);
    pMetaD = (AL_TMetaData *)AL_DisplayInfoMetaData_Clone(pDispMeta);
    if (!pMetaD)
        throw std::runtime_error("Clone of PixMapMetaData was not created!");
    if (!AL_Buffer_AddMetaData(frame_, pMetaD))
        throw std::runtime_error("Cloned pMetaD did not get added!\n");
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
    frame_ = AL_PixMapBuffer_Create_And_AddPlanes(AL_GetDefaultAllocator(), sDestroyFrame, tDim,
                                                   tRoundedDim, tPicFormat, 1, "IO frame buffer");
    if (!frame_)
    {
        throw std::runtime_error("Failed to create buffer");
    }
    AL_Buffer_Ref(frame_);
    AL_PixMapBuffer_SetDimension(frame_, tDim);
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

Frame::~Frame()
{
    if (frame_)
    {
        if (callback_)
        {
            callback_(*this);
        }
        AL_Buffer_Unref(frame_);
        frame_ = nullptr;
    }
}

void Frame::invalidate()
{
    if (frame_)
    {
        AL_Buffer_InvalidateMemory(frame_);
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


AL_TBuffer *Frame::getBuffer() const { return frame_; }

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
    return AL_PixMapBuffer_GetFourCC(frame_);
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

/// Link the life cycle of this frame to another frame.
void Frame::link(Ptr<Frame> frame)
{
    linkedFrame_ = frame;
}

// class  FrameQueue

FrameQueue::FrameQueue() {}

FrameQueue::~FrameQueue() {}

void FrameQueue::setReturnQueueSize(int size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    returnQueueSize_ = std::max(size, 0);
    resizeReturnQueue();
}

void FrameQueue::enqueue(Ptr<Frame> frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(frame);
    cv_.notify_one();
}

Ptr<Frame> FrameQueue::dequeue(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    resizeReturnQueue();

    if (cv_.wait_for(lock, timeout, [this]{ return !queue_.empty(); }))
    {
        Ptr<Frame> frame = queue_.front();
        queue_.pop();
        if (returnQueueSize_ > 0)
        {
            returnQueue_.push(frame);
        }
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
    while (!returnQueue_.empty())
    {
        returnQueue_.pop();
    }
    while (!queue_.empty())
    {
        queue_.pop();
    }
}

void FrameQueue::resizeReturnQueue()
{ // call when mutex_ is locked;
    while (!returnQueue_.empty() && (returnQueue_.size() >= returnQueueSize_)) // leave one space free
    {
        returnQueue_.pop();
    }
}

} // namespace vcucodec
} // namespace cv

