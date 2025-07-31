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
#ifndef OPENCV_VCUCODEC_VCUREADER_HPP
#define OPENCV_VCUCODEC_VCUREADER_HPP

#include <opencv2/core.hpp>

extern "C" {
#include "config.h"
#include "lib_decode/lib_decode.h"
#include "lib_app/BufPool.h"
}

#include <memory>
#include <string_view>

namespace cv {
namespace vcucodec {

class Reader
{
public:
    virtual ~Reader() = default;
    virtual bool setPath(std::string_view filePath) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    static std::unique_ptr<Reader> createReader(AL_HDecoder hDec, BufPool& bufPool);
};

} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUREADER_HPP