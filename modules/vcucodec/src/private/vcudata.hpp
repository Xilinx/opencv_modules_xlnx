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
#ifndef OPENCV_VCUCODEC_VCUDATA_HPP
#define OPENCV_VCUCODEC_VCUDATA_HPP

#include <opencv2/core.hpp>



// Forward declarations for VCU2 types
extern "C" {
typedef struct AL_TBuffer AL_TBuffer;
typedef void* AL_HEncoder;
}

namespace cv {
namespace vcucodec {

class RawInfo;

/// Class Data represents a decoded Data with its associated metadata and lifecycle management.
class Data
{
    /// Construct Data with pre-existing buffer and info.
    Data(AL_TBuffer* data, AL_HEncoder hEnc);
public:
    ~Data();

    /// Create
    static Ptr<Data> create(AL_TBuffer* data, AL_HEncoder hEnc);
    AL_TBuffer* buf() const { return data_; }

    /// Walk through the internal buffers and call the provided callback for each buffer.
    /// @return The number of video frames walked
    int32_t walkBuffers(std::function<void(size_t size, uint8_t* data)>) const;

private:
    AL_TBuffer* data_;
    AL_HEncoder hEnc_;
};


} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUDATA_HPP