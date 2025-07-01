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

#ifdef HAVE_VCU2_CTRLSW
extern "C" {
// Include VCU2 Control Software headers
#include "lib_common/BufferAPI.h"
#include "lib_common/Profiles.h"
#include "lib_common/SliceConsts.h"
#include "lib_common/RoundUp.h"
}

// Forward declarations for VCU2 types
typedef struct AL_TDecoder AL_TDecoder;
typedef struct AL_TEncoder AL_TEncoder;
typedef struct AL_TBuffer AL_TBuffer;

namespace cv {
namespace vcucodec {
namespace vcu2 {

// VCU2 utility functions
void testAllegroDecoderInitRiscV();

// Buffer management functions (placeholders for future implementation)
// AL_TBuffer* createVCU2Buffer(int width, int height, int format);
// void releaseVCU2Buffer(AL_TBuffer* buffer);
// bool copyMatToVCU2Buffer(const Mat& src, AL_TBuffer* dst);
// bool copyVCU2BufferToMat(AL_TBuffer* src, Mat& dst);

} // namespace vcu2
} // namespace vcucodec
} // namespace cv

#else
// Stub definitions when VCU2 is not available
namespace cv {
namespace vcucodec {
namespace vcu2 {

inline void testAllegroDecoderInitRiscV() { /* no-op */ }

} // namespace vcu2
} // namespace vcucodec
} // namespace cv

#endif // HAVE_VCU2_CTRLSW

#endif // OPENCV_VCUCODEC_VCUUTILS_HPP