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

// VideoFrameImpl: concrete VideoFrame backed by a hardware decoder buffer.
//
// The public VideoFrame interface (vcucodec.hpp) exposes only copy-safe
// CV_WRAP methods.  This class adds two non-virtual helpers for the
// hand-written zero-copy Python binding (pyopencv_vcucodec.hpp):
//
//   planeRef(index)  — const Mat& wrapping the HW buffer (no copy)
//   pin()            — shared_ptr that prevents the HW buffer from being
//                      recycled while any numpy view exists
//
// The binding code obtains a VideoFrameImpl* via dynamic_cast from the
// Ptr<VideoFrame> it receives from Python.

#ifndef OPENCV_VCUCODEC_VIDEOFRAMEIMPL_HPP
#define OPENCV_VCUCODEC_VIDEOFRAMEIMPL_HPP

#include <opencv2/vcucodec.hpp>
#include <memory>
#include <mutex>
#include <vector>
#include "opencv2/core/utils/logger.hpp"

namespace cv {
namespace vcucodec {

// Forward declaration — defined in vcuframe.hpp (private)
class Frame;

/// Prevent a single HW buffer from being recycled.
/// Created by VideoFrameImpl; held alive by the PyCapsule returned from pin().
/// PinRegistry::revokeAll() clears the inner Ptr<Frame>, releasing the buffer
/// even if the PyCapsule (and thus the PinAnchor shared_ptr) still exists.
struct PinAnchor
{
    Ptr<Frame> frame;
    explicit PinAnchor(Ptr<Frame> f) : frame(std::move(f)) {}
    void revoke() { frame.reset(); }
};

/// Tracks every PinAnchor created by a decoder.
/// On decoder teardown, revokeAll() forcibly releases every still-pinned
/// buffer so the Allegro pool can be destroyed without assertions.
class PinRegistry
{
public:
    void track(const std::shared_ptr<PinAnchor>& anchor)
    {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.push_back(anchor);
    }

    void revokeAll()
    {
        std::lock_guard<std::mutex> lk(mu_);
        int revoked = 0;
        for (auto& wp : entries_)
            if (auto sp = wp.lock()) { sp->revoke(); ++revoked; }
        if (revoked > 0)
            CV_LOG_WARNING(NULL, "PinRegistry: revoked " << revoked
                           << " outstanding pin(s) -- leaked numpy views?");
        entries_.clear();
    }

private:
    std::mutex mu_;
    std::vector<std::weak_ptr<PinAnchor>> entries_;
};

/// Concrete VideoFrame backed by a hardware decoder Frame.
///
/// Constructed in vcudec.cpp where the Frame and HW buffer types are visible.
/// Only the zero-copy accessors (planeRef, pin) and the VideoFrame overrides
/// are declared here; the constructor and method bodies live in vcudec.cpp.
class VideoFrameImpl : public VideoFrame
{
public:
    /// Constructed by VCUDecoder::nextFrame() in vcudec.cpp.
    VideoFrameImpl(Ptr<Frame> frame, const RawInfo& info,
                   std::vector<Mat> srcPlanes,
                   const std::shared_ptr<PinRegistry>& registry = nullptr);



    // -- VideoFrame overrides --
    const RawInfo& info() const override;
    std::vector<Mat> planes() const override;
    void copyToVec(std::vector<Mat>& planes) const override;
    void copyTo(Mat& dst, int stride = 0) const override;
    void convertTo(Mat& dst, int fourCC) const override;

    // -- Zero-copy accessors (used by pyopencv_vcucodec.hpp via dynamic_cast) --

    /// Return a const reference to the Mat header wrapping hardware buffer
    /// plane @p index.  The Mat does NOT own the data.
    const Mat& planeRef(int index) const;

    /// Pin the hardware buffer so it is not recycled.  The returned
    /// shared_ptr must be kept alive (e.g. as a PyCapsule base) for as
    /// long as any zero-copy numpy view exists.
    std::shared_ptr<void> pin() const;

private:
    void convertColor(Mat& dst, int targetFourcc) const;

    std::shared_ptr<PinAnchor> anchor_; ///< Prevents HW buffer reclamation; revocable by PinRegistry.
    RawInfo info_;                        ///< Frame metadata (post-crop).
    std::vector<Mat> srcPlanes_;          ///< Mat headers wrapping HW buffer planes (no data copy).
};

} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VIDEOFRAMEIMPL_HPP
