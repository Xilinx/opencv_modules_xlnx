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
#include "opencv2/vcutypes.hpp"
#include "lib_common_enc/Settings.h"

#define ENUMASSERT(x,y) static_assert(static_cast<int>(x) == static_cast<int>(y))

namespace cv {
namespace vcucodec {

ENUMASSERT(PicStruct::FRAME, AL_PS_FRM);
ENUMASSERT(PicStruct::TOP, AL_PS_TOP_FLD);
ENUMASSERT(PicStruct::BOT, AL_PS_BOT_FLD);
ENUMASSERT(PicStruct::TOP_BOT, AL_PS_TOP_BOT);
ENUMASSERT(PicStruct::BOT_TOP, AL_PS_BOT_TOP);
ENUMASSERT(PicStruct::TOP_BOT_TOP, AL_PS_TOP_BOT_TOP);
ENUMASSERT(PicStruct::BOT_TOP_BOT, AL_PS_BOT_TOP_BOT);

ENUMASSERT(PicStruct::FRAME_X2, AL_PS_FRM_x2);
ENUMASSERT(PicStruct::FRAME_X3, AL_PS_FRM_x3);
ENUMASSERT(PicStruct::TOP_PREV_BOT, AL_PS_TOP_FLD_WITH_PREV_BOT);
ENUMASSERT(PicStruct::BOT_PREV_TOP, AL_PS_BOT_FLD_WITH_PREV_TOP);
ENUMASSERT(PicStruct::TOP_NEXT_BOT, AL_PS_TOP_FLD_WITH_NEXT_BOT);
ENUMASSERT(PicStruct::BOT_NEXT_TOP, AL_PS_BOT_FLD_WITH_NEXT_TOP);

}  // namespace vcucodec
}  // namespace cv