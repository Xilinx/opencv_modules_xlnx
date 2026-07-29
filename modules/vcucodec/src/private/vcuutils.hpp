/*
   Copyright (c) 2025-2026  Advanced Micro Devices, Inc. (AMD)

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
#ifndef OPENCV_VCUCODEC_VCUUTILS_HPP
#define OPENCV_VCUCODEC_VCUUTILS_HPP

#include "opencv2/core.hpp"
#include "opencv2/vcucodec.hpp"

extern "C" {
#include "lib_common/Error.h"
#include "lib_common/FourCC.h"
#include "lib_common/HDR.h"
#include "lib_common/PicFormat.h"
#include "lib_common_enc/EncChanParam.h"
}

#include <fstream>
namespace cv {
namespace vcucodec {

// AL_TEncChanParam::iQPTableDepth (QP-table CTB depth) is a VCU2-only channel parameter. VCU1
// (vcu-ctrl-sw) has no such field and its QP-table code treats the depth as 0, so return 0 there.
// Centralizing the access keeps the VCU1/VCU2 divergence in one guarded place.
static inline int32_t qpTableDepth(const AL_TEncChanParam& chn)
{
#ifdef HAVE_VCU2_CTRLSW
    return chn.iQPTableDepth;
#else
    (void)chn;
    return 0;
#endif
}

// AL_GetFourCC() takes an AL_TPicFormat const* in vcu2-ctrl-sw master-next, but takes it by
// value in vcu-ctrl-sw (VCU1), VDU, and pre-master-next vcu2-ctrl-sw. Centralize the divergence
// so callers stay API-agnostic.
static inline TFourCC getFourCC(const AL_TPicFormat& fmt)
{
#ifdef HAVE_VCU2_CTRLSW
    return AL_GetFourCC(&fmt);
#else
    return AL_GetFourCC(fmt);
#endif
}

// Map an OpenCV-API encoder input FourCC (kernel-aligned "proper" naming, where the LSB-packed
// 10/12-bit semi-planar formats are P0AL/P0CL/P2AL/P2CL) to the FourCC understood by the
// underlying ctrl-sw encoder:
//   - VCU2 (vcu2-ctrl-sw) uses the new naming natively: P0AL/P0CL/P2AL/P2CL are the LSB-packed
//     formats the encoder accepts, while P010/P012/P210/P212 are the MSB-aligned variants (which
//     the encoder does NOT accept). Nothing to translate.
//   - VCU1 (vcu-ctrl-sw) / VDU predate the MSB/LSB split; there P010/P012/P210/P212 ARE the
//     LSB-packed formats, so map the API's LSB names onto them.
// Keeping the OpenCV interface on the proper (P0AL...) names hides this divergence from users.
static inline int toEncoderFourCC(int fourcc)
{
#ifdef HAVE_VCU2_CTRLSW
    return fourcc;
#else
    switch (fourcc)
    {
    case FOURCC(P0AL): return FOURCC(P010);
    case FOURCC(P0CL): return FOURCC(P012);
    case FOURCC(P2AL): return FOURCC(P210);
    case FOURCC(P2CL): return FOURCC(P212);
    default:           return fourcc;
    }
#endif
}

bool operator==(const RawInfo& lhs, const RawInfo& rhs);
bool operator!=(const RawInfo& lhs, const RawInfo& rhs);

class OutputStream
{
public:
    OutputStream(const String& filename, bool binary);
    ~OutputStream();

    std::ofstream& operator()() { return file_; }

private:
    std::ofstream file_;
};

class en_codec_error : public std::runtime_error
{
public:
    explicit en_codec_error(const std::string& _Message, AL_ERR errCode)
        : std::runtime_error(_Message), errorCode_(errCode) {}

    explicit en_codec_error(const char* _Message, AL_ERR errCode)
        : std::runtime_error(_Message), errorCode_(errCode) {}

    AL_ERR getCode() const { return errorCode_; }

protected:
    AL_ERR errorCode_;
};

template <typename T, typename F>
void convert(T& to, const F& from);

template <> void convert(HDRSEIs& to, const AL_THDRSEIs& from);
template <> void convert(AL_THDRSEIs& to, const HDRSEIs& from);

// Template specialization for AL_TPicFormat toString
template<> String toString<AL_TPicFormat>(AL_TPicFormat const& format);

struct FormatInfo
{
    FormatInfo(int fourcc);

    int fourcc;
    bool decodeable;
    bool encodeable;

    AL_TPicFormat const& format;

    static String getFourCCs(bool decoder); // false for encoder
};

} // namespace vcucodec
} // namespace cv


#endif // OPENCV_VCUCODEC_VCUUTILS_HPP