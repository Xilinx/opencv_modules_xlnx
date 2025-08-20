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
#ifndef OPENCV_VCUCODEC_VCURAWOUT_HPP
#define OPENCV_VCUCODEC_VCURAWOUT_HPP

#include "vcuframe.hpp"

namespace cv {
namespace vcucodec {

/// Class RawOutput handles the output of raw frames, including conversion.
class RawOutput
{
public:
    virtual ~RawOutput() = default;

    /// Configure the raw output with the specified fourcc and bit depth.
    virtual void configure(int fourcc, unsigned int bitDepth, int max_frames,
                           int szReturnQueue) = 0;

    /// Process a frame and enqueue it for output.
    virtual bool process(Ptr<Frame> frame, int32_t iBitDepthAlloc,
                         bool& bIsMainDisplay, bool& bNumFrameReached, bool bDecoderExists) = 0;

    /// Dequeue a processed frame.
    virtual Ptr<Frame> dequeue(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) = 0;

    /// Check if the queue is idle.
    virtual bool idle() = 0;

    /// Flush the output queue.
    virtual void flush() = 0;

    static Ptr<RawOutput> create();
};


} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCURAWOUT_HPP