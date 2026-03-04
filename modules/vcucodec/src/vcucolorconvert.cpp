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

#include "opencv2/vcucolorconvert.hpp"

#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////
//  FOURCC Helpers (internal)

/// Build a FOURCC code from four characters at compile time.
#define VCU_FOURCC(a, b, c, d) \
    ( (int)(uint8_t)(a)        | ((int)(uint8_t)(b) << 8) | \
      ((int)(uint8_t)(c) << 16) | ((int)(uint8_t)(d) << 24) )

static constexpr int VCU_FOURCC_BGR  = VCU_FOURCC('B','G','R',' ');
static constexpr int VCU_FOURCC_BGRA = VCU_FOURCC('B','G','R','A');

////////////////////////////////////////////////////////////////////////////////////////////////////
//  FOURCC format descriptor (internal)

namespace { // anonymous

/// Chroma subsampling and bytes-per-pixel info for known fourcc codes.
struct FourccInfo
{
    int  bpp;          ///< Bytes per pixel for plane 0 (luma / packed).
    int  chromaW;      ///< Horizontal chroma subsampling divisor (1 = none, 2 = 4:2:x).
    int  chromaH;      ///< Vertical chroma subsampling divisor (1 = 4:4:x/4:2:2, 2 = 4:2:0).
    int  numPlanes;    ///< Number of planes.
    int  chromaBpp;    ///< Bytes per pixel for chroma plane (interleaved UV = 2, separate U/V = 1).
};

/// Return format info for a known fourcc, or nullptr if unknown.
const FourccInfo* fourccInfo(int fourcc)
{
    // Common fourcc codes.  NV12/NV16 are 8-bit; P010/P012/P210/P212 are 16-bit per component.
    static const std::map<int, FourccInfo> table = {
        // Semi-planar 4:2:0 — 8-bit
        { VCU_FOURCC('N','V','1','2'), { 1, 2, 2, 2, 2 } },
        // Semi-planar 4:2:0 — 10/12-bit (16-bit storage)
        { VCU_FOURCC('P','0','1','0'), { 2, 2, 2, 2, 4 } },
        { VCU_FOURCC('P','0','1','2'), { 2, 2, 2, 2, 4 } },
        // Semi-planar 4:2:2 — 8-bit
        { VCU_FOURCC('N','V','1','6'), { 1, 2, 1, 2, 2 } },
        // Semi-planar 4:2:2 — 10/12-bit (16-bit storage)
        { VCU_FOURCC('P','2','1','0'), { 2, 2, 1, 2, 4 } },
        { VCU_FOURCC('P','2','1','2'), { 2, 2, 1, 2, 4 } },
        // Planar 4:2:0 — 8-bit
        { VCU_FOURCC('I','4','2','0'), { 1, 2, 2, 3, 1 } },
        { VCU_FOURCC('Y','V','1','2'), { 1, 2, 2, 3, 1 } },
        // Planar 4:2:2 — 8-bit
        { VCU_FOURCC('I','4','2','2'), { 1, 2, 1, 3, 1 } },
        // Packed BGR/BGRA
        { VCU_FOURCC_BGR,              { 3, 1, 1, 1, 0 } },
        { VCU_FOURCC_BGRA,             { 4, 1, 1, 1, 0 } },
        // Gray
        { VCU_FOURCC('G','R','E','Y'), { 1, 1, 1, 1, 0 } },
        { VCU_FOURCC('Y','8','0','0'), { 1, 1, 1, 1, 0 } },
    };

    auto it = table.find(fourcc);
    return (it != table.end()) ? &it->second : nullptr;
}

}  // anonymous namespace

////////////////////////////////////////////////////////////////////////////////////////////////////
//  ColorConverter::Surface::crop

ColorConverter::Surface ColorConverter::Surface::crop(int x, int y, int w, int h) const
{
    // Clamp origin to surface bounds.
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= width)  x = width;
    if (y >= height) y = height;

    // Default: remaining region from (x, y).
    int maxW = width  - x;
    int maxH = height - y;
    if (w <= 0) w = maxW;
    if (h <= 0) h = maxH;

    // Clamp to what fits.
    if (w > maxW) w = maxW;
    if (h > maxH) h = maxH;

    Surface result = *this;  // copy fourcc, matrix, fullRange, etc.
    result.width  = w;
    result.height = h;

    const FourccInfo* info = fourccInfo(fourcc);

    // Plane 0 (luma or packed): offset by (y * step + x * bpp).
    int bpp0 = info ? info->bpp : 1;
    if (plane[0])
        result.plane[0] = plane[0] + static_cast<size_t>(y) * step[0]
                                   + static_cast<size_t>(x) * bpp0;

    if (info && info->numPlanes >= 2 && plane[1])
    {
        int cx = x / info->chromaW;
        int cy = y / info->chromaH;
        result.plane[1] = plane[1] + static_cast<size_t>(cy) * step[1]
                                   + static_cast<size_t>(cx) * info->chromaBpp;
    }

    if (info && info->numPlanes >= 3 && plane[2])
    {
        int cx = x / info->chromaW;
        int cy = y / info->chromaH;
        result.plane[2] = plane[2] + static_cast<size_t>(cy) * step[2]
                                   + static_cast<size_t>(cx) * info->chromaBpp;
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//  ColorConverter registry

namespace {

using FourccPair = std::pair<int, int>;

struct Registry
{
    std::mutex                                               mu;
    std::map<FourccPair, std::shared_ptr<ColorConverter>>    converters;

    static Registry& instance()
    {
        static Registry reg;
        return reg;
    }
};

}  // anonymous namespace

void ColorConverter::add(int srcFourcc, int dstFourcc,
                         const std::shared_ptr<ColorConverter>& converter)
{
    if (!converter)
        throw std::invalid_argument("ColorConverter::add: converter must not be null");

    auto& reg = Registry::instance();
    std::lock_guard<std::mutex> lock(reg.mu);
    reg.converters[{srcFourcc, dstFourcc}] = converter;
}

std::shared_ptr<ColorConverter> ColorConverter::find(int srcFourcc, int dstFourcc)
{
    auto& reg = Registry::instance();
    std::lock_guard<std::mutex> lock(reg.mu);
    auto it = reg.converters.find({srcFourcc, dstFourcc});
    return (it != reg.converters.end()) ? it->second : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Built-in: NV12 → BGR / BGRA  (delegates to cv::cvtColorTwoPlane)

class NV12toBGRConverter : public ColorConverter
{
    int code_;  // cv::COLOR_YUV2BGR_NV12 or cv::COLOR_YUV2BGRA_NV12

public:
    explicit NV12toBGRConverter(int code) : code_(code) {}

    void convert(const Surface& src, Surface& dst) const override
    {
        const int w = std::min(src.width, dst.width);
        const int h = std::min(src.height, dst.height);
        if (w <= 0 || h <= 0) return;

        // Wrap source planes as cv::Mat headers (zero-copy).
        cv::Mat Y (h,     w,     CV_8UC1, src.plane[0], src.step[0]);
        cv::Mat UV(h / 2, w / 2, CV_8UC2, src.plane[1], src.step[1]);

        // Wrap destination as cv::Mat header.
        int ch = (code_ == cv::COLOR_YUV2BGRA_NV12) ? 4 : 3;
        cv::Mat out(h, w, CV_MAKETYPE(CV_8U, ch), dst.plane[0], dst.step[0]);

        cv::cvtColorTwoPlane(Y, UV, out, code_);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Built-in: GRAY → BGR / BGRA  (delegates to cv::cvtColor)

class GRAYtoBGRConverter : public ColorConverter
{
    int code_;  // cv::COLOR_GRAY2BGR or cv::COLOR_GRAY2BGRA

public:
    explicit GRAYtoBGRConverter(int code) : code_(code) {}

    void convert(const Surface& src, Surface& dst) const override
    {
        const int w = std::min(src.width, dst.width);
        const int h = std::min(src.height, dst.height);
        if (w <= 0 || h <= 0) return;

        cv::Mat gray(h, w, CV_8UC1, src.plane[0], src.step[0]);

        int ch = (code_ == cv::COLOR_GRAY2BGRA) ? 4 : 3;
        cv::Mat out(h, w, CV_MAKETYPE(CV_8U, ch), dst.plane[0], dst.step[0]);

        cv::cvtColor(gray, out, code_);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Auto-registration at static init

static const int builtinRegistered = []() -> int
{
    static constexpr int NV12 = VCU_FOURCC('N','V','1','2');
    static constexpr int GREY = VCU_FOURCC('G','R','E','Y');
    static constexpr int Y800 = VCU_FOURCC('Y','8','0','0');

    // NV12 → BGR / BGRA  (delegates to cv::cvtColorTwoPlane)
    auto nv12_bgr  = std::make_shared<NV12toBGRConverter>(cv::COLOR_YUV2BGR_NV12);
    auto nv12_bgra = std::make_shared<NV12toBGRConverter>(cv::COLOR_YUV2BGRA_NV12);
    ColorConverter::add(NV12, VCU_FOURCC_BGR,  nv12_bgr);
    ColorConverter::add(NV12, VCU_FOURCC_BGRA, nv12_bgra);

    // GRAY → BGR / BGRA  (delegates to cv::cvtColor)
    auto gray_bgr  = std::make_shared<GRAYtoBGRConverter>(cv::COLOR_GRAY2BGR);
    auto gray_bgra = std::make_shared<GRAYtoBGRConverter>(cv::COLOR_GRAY2BGRA);
    ColorConverter::add(GREY, VCU_FOURCC_BGR,  gray_bgr);
    ColorConverter::add(GREY, VCU_FOURCC_BGRA, gray_bgra);
    ColorConverter::add(Y800, VCU_FOURCC_BGR,  gray_bgr);
    ColorConverter::add(Y800, VCU_FOURCC_BGRA, gray_bgra);

    return 1;
}();
