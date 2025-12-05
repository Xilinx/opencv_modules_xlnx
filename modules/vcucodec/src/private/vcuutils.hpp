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
#ifndef OPENCV_VCUCODEC_VCUUTILS_HPP
#define OPENCV_VCUCODEC_VCUUTILS_HPP

#include "opencv2/core.hpp"
#include "opencv2/vcucodec.hpp"

extern "C" {
#include "lib_common/Error.h"
#include "lib_common/FourCC.h"
#include "lib_common/HDR.h"
#include "lib_common/PicFormat.h"
}

#include <fstream>
namespace cv {
namespace vcucodec {

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