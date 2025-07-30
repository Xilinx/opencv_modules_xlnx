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
#ifndef OPENCV_VCUCODEC_VCUFRAME_HPP
#define OPENCV_VCUCODEC_VCUFRAME_HPP

#include <opencv2/core.hpp>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <memory>
#include <queue>

// Forward declarations for VCU2 types
extern "C" {
typedef struct AL_TBuffer AL_TBuffer;
typedef struct AL_TInfoDecode AL_TInfoDecode;
typedef struct AL_TCropInfo AL_TCropInfo;
typedef struct AL_TDimension AL_TDimension;
}

namespace cv {
namespace vcucodec {

/// Class Frame represents a decoded frame with its associated metadata and lifecycle management.
class Frame
{
    using FrameCB = std::function<void(Frame const &)>; ///< Callback after frame processing

    /// Construct frame with pre-existing buffer and info.
    Frame(AL_TBuffer *pFrame, AL_TInfoDecode const *pInfo, FrameCB cb = FrameCB());

    Frame(Frame const &frame); ///< shallow copy constructor

    Frame(Size const &size, int fourcc); ///< Constructor for YUV conversion buffer

public:
    ~Frame();

    void invalidate();
    AL_TBuffer *getBuffer() const;
    AL_TInfoDecode const &getInfo() const;
    bool isMainOutput() const;
    unsigned int bitDepthY() const;
    unsigned int bitDepthUV() const;
    AL_TCropInfo const &getCropInfo() const;
    AL_TDimension const &getDimension() const;
    int getFourCC() const;
    void link(Ptr<Frame> frame);

    /// Create a new frame from an existing buffer and info.
    static Ptr<Frame> create(AL_TBuffer *pFrame, AL_TInfoDecode const *pInfo, FrameCB cb = {});

    /// Create a shallow copy of an existing frame.
    static Ptr<Frame> createShallowCopy(Ptr<Frame> const &frame);

    /// Create a new frame for YUV input/output with specified size and fourcc.
    static Ptr<Frame> createYuvIO(Size const &size, int fourcc);


private:
    AL_TBuffer* frame_ = nullptr;
    std::unique_ptr<AL_TInfoDecode> info_;
    Ptr<Frame> linkedFrame_;
    FrameCB callback_;
};

class FrameQueue
{
public:
    void enqueue(Ptr<Frame> frame);
    Ptr<Frame> dequeue(std::chrono::milliseconds timeout);
    bool empty();
    void clear();
private:
    std::queue<Ptr<Frame>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};


} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUFRAME_HPP