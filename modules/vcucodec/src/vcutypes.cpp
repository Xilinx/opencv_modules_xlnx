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
#include "config.h"
#include "lib_common_enc/Settings.h"

#define ENUMASSERT(x,y) static_assert(static_cast<int>(x) == static_cast<int>(y))

namespace cv {
namespace vcucodec {

// AL_EPicStruct
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
ENUMASSERT(13, AL_PS_MAX_ENUM);

// AL_ERateCtrlMode
ENUMASSERT(RCMode::CONST_QP, AL_RC_CONST_QP);
ENUMASSERT(RCMode::CBR, AL_RC_CBR);
ENUMASSERT(RCMode::VBR, AL_RC_VBR);
ENUMASSERT(RCMode::LOW_LATENCY, AL_RC_LOW_LATENCY);
ENUMASSERT(RCMode::CAPPED_VBR, AL_RC_CAPPED_VBR);
ENUMASSERT(65, AL_RC_MAX_ENUM);

// AL_EEntropyMode
ENUMASSERT(Entropy::CAVLC, AL_MODE_CAVLC);
ENUMASSERT(Entropy::CABAC, AL_MODE_CABAC);
ENUMASSERT(2, AL_MODE_MAX_ENUM);

// AL_EGopCtrlMode
ENUMASSERT(GOPMode::BASIC, AL_GOP_MODE_DEFAULT);
ENUMASSERT(GOPMode::BASIC_B, AL_GOP_MODE_DEFAULT_B);
ENUMASSERT(GOPMode::PYRAMIDAL, AL_GOP_MODE_PYRAMIDAL);
ENUMASSERT(GOPMode::PYRAMIDAL_B, AL_GOP_MODE_PYRAMIDAL_B);
ENUMASSERT(GOPMode::LOW_DELAY_P, AL_GOP_MODE_LOW_DELAY_P);
ENUMASSERT(GOPMode::LOW_DELAY_B, AL_GOP_MODE_LOW_DELAY_B);
ENUMASSERT(GOPMode::ADAPTIVE, AL_GOP_MODE_ADAPTIVE);
ENUMASSERT(17, AL_GOP_MODE_MAX_ENUM);

// AL_EGdrMode
ENUMASSERT(GDRMode::DISABLE, AL_GDR_OFF);
ENUMASSERT(GDRMode::VERTICAL, AL_GDR_VERTICAL);
ENUMASSERT(GDRMode::HORIZONTAL, AL_GDR_HORIZONTAL);
ENUMASSERT(4, AL_GDR_MAX_ENUM);

}  // namespace vcucodec
}  // namespace cv