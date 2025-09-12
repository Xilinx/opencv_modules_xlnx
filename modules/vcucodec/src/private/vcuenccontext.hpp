
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
#ifndef OPENCV_VCUCODEC_VCUENCCONTEXT_HPP
#define OPENCV_VCUCODEC_VCUENCCONTEXT_HPP

#include <opencv2/core.hpp>

#include <memory>
#include <vector>

struct ConfigFile;
struct AL_TBuffer;

namespace cv {
namespace vcucodec {

class Device;

using DataCallback = std::function<void(std::vector<std::string_view>&)>;

class EncContext
{
public:
    virtual ~EncContext() = default;
    virtual void writeFrame(std::shared_ptr<AL_TBuffer> frame) = 0;
    virtual std::shared_ptr<AL_TBuffer> getSharedBuffer() = 0;
    virtual bool waitForCompletion() = 0;
    virtual void notifyGMV(int32_t frameIndex, int32_t gmVectorX, int32_t gmVectorY) = 0;

    static Ptr<EncContext> create(ConfigFile& cfg, Ptr<Device>& device, DataCallback dataCallback);
};

} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUENCCONTEXT_HPP
