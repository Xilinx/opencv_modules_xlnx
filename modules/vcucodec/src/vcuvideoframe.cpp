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
#include "vcuvideoframe.hpp"
#include "opencv2/vcucolorconvert.hpp"
#include "vcuutils.hpp"

#include <cstring>

namespace cv {
namespace vcucodec {
namespace {

const int fourcc_BGR  = 0x20524742; // "BGR " — can't use FOURCC() macro (ignores spaces)
const int fourcc_BGRA = FOURCC(BGRA);

} // anonymous namespace

// ---- VideoFrameImpl method definitions ----

VideoFrameImpl::VideoFrameImpl(Ptr<Frame> frame, const RawInfo& info,
                               std::vector<Mat> srcPlanes,
                               const std::shared_ptr<PinRegistry>& registry)
    : anchor_(std::make_shared<PinAnchor>(std::move(frame))), info_(info),
      srcPlanes_(std::move(srcPlanes))
{
    if (registry) registry->track(anchor_);
}

const RawInfo& VideoFrameImpl::info() const { return info_; }
std::vector<Mat> VideoFrameImpl::planes() const { return srcPlanes_; }

void VideoFrameImpl::copyToVec(std::vector<Mat>& planes) const
{
    planes.resize(srcPlanes_.size());
    for (size_t i = 0; i < srcPlanes_.size(); ++i)
        planes[i] = srcPlanes_[i].clone();
}

void VideoFrameImpl::copyTo(Mat& dst, int stride) const
{
    int bpp = (info_.bitsPerLuma > 8) ? 2 : 1;
    int yWidthBytes = info_.width * bpp;
    int yPitch = (stride > 0) ? stride : yWidthBytes;
    CV_Assert(yPitch >= yWidthBytes);

    // Compute total height across all planes
    int nPlanes = (int)srcPlanes_.size();
    int totalHeight = 0;
    for (int i = 0; i < nPlanes; i++)
        totalHeight += srcPlanes_[i].rows;

    // Allocate contiguous byte buffer; zero-fill for chroma padding
    dst.create(totalHeight, yPitch, CV_8UC1);
    dst = Scalar(0);

    uint8_t* dstPtr = dst.ptr<uint8_t>();
    for (int i = 0; i < nPlanes; i++)
    {
        const Mat& src = srcPlanes_[i];
        // Actual data bytes per source row: cols × channels × element size
        int dataRowBytes = src.cols * src.channels() * (int)src.elemSize1();
        int copyBytes = std::min(dataRowBytes, yPitch);

        for (int y = 0; y < src.rows; y++)
        {
            std::memcpy(dstPtr, src.ptr(y), copyBytes);
            dstPtr += yPitch;
        }
    }
}

void VideoFrameImpl::convertTo(Mat& dst, int fourCC) const
{
    CV_Assert(fourCC == fourcc_BGR || fourCC == fourcc_BGRA);
    convertColor(dst, fourCC);
}

const Mat& VideoFrameImpl::planeRef(int index) const
{
    CV_Assert(index >= 0 && index < (int)srcPlanes_.size());
    return srcPlanes_[index];
}

std::shared_ptr<void> VideoFrameImpl::pin() const
{
    // Return the PinAnchor as a shared_ptr<void>.  While any PyCapsule
    // (or other caller) holds this, the HW buffer stays pinned — unless
    // PinRegistry::revokeAll() clears anchor_->frame first.
    return anchor_;
}

void VideoFrameImpl::convertColor(Mat& dst, int targetFourcc) const
{
    auto conv = ColorConverter::find(info_.fourcc, targetFourcc);
    if (!conv)
        CV_Error(Error::StsNotImplemented,
                 cv::format("No ColorConverter registered for fourcc %08x -> %08x",
                            info_.fourcc, targetFourcc));

    int nPlanes = (int)srcPlanes_.size();

    ColorConverter::Surface srcS = { info_.fourcc, {}, {}, srcPlanes_[0].cols, srcPlanes_[0].rows,
        static_cast<int>(info_.colourMatrixCoeffs)
    };
    for (int i = 0; i < nPlanes && i < 3; ++i)
    {
        srcS.plane[i] = srcPlanes_[i].data;
        srcS.step[i]  = srcPlanes_[i].step;
    }

    int ch = (targetFourcc == fourcc_BGRA) ? 4 : 3;
    dst.create(srcS.height, srcS.width, CV_MAKETYPE(srcPlanes_[0].depth(), ch));

    ColorConverter::Surface dstS = { targetFourcc, { dst.data }, { dst.step }, dst.cols, dst.rows };

    conv->convert(srcS, dstS);
}

} // namespace vcucodec
} // namespace cv
