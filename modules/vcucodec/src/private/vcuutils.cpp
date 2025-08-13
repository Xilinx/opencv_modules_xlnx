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
#include "vcuutils.hpp"

#ifdef HAVE_VCU2_CTRLSW
extern "C" {
#include "config.h"
#include "lib_decode/lib_decode.h"
#include "lib_common_dec/DecoderArch.h"
}
#endif

namespace cv {
namespace vcucodec {

bool operator==(const RawInfo& lhs, const RawInfo& rhs)
{
    if (lhs.eos || rhs.eos)
        return false;
    bool equal = lhs.fourcc == rhs.fourcc &&
        lhs.bitsPerLuma == rhs.bitsPerLuma &&
        lhs.bitsPerChroma == rhs.bitsPerChroma &&
        lhs.stride == rhs.stride &&
        lhs.width == rhs.width &&
        lhs.height == rhs.height &&
        lhs.pos_x == rhs.pos_x &&
        lhs.pos_y == rhs.pos_y &&
        lhs.crop_top == rhs.crop_top &&
        lhs.crop_bottom == rhs.crop_bottom &&
        lhs.crop_left == rhs.crop_left &&
        lhs.crop_right == rhs.crop_right;
    return equal;
}

bool operator!=(const RawInfo& lhs, const RawInfo& rhs)
{
    return !(lhs == rhs);
}

} // namespace vcucodec
} // namespace cv