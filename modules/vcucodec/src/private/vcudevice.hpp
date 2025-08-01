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
#ifndef OPENCV_VCUCODEC_VCUDEVICE_HPP
#define OPENCV_VCUCODEC_VCUDEVICE_HPP

#include <opencv2/core.hpp>

#include <memory>

extern "C" {
typedef struct AL_TAllocator AL_TAllocator;
typedef struct AL_ITimer AL_ITimer;
}

namespace cv {
namespace vcucodec {

class Device
{
public:
    virtual ~Device() = default;
    virtual void* getScheduler() = 0;
    virtual void* getCtx() = 0;
    virtual AL_TAllocator* getAllocator() = 0;
    virtual AL_ITimer* getTimer() = 0;

    static Ptr<Device> create();
};

} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUDEVICE_HPP