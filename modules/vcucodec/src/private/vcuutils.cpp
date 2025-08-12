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
namespace utils {

}  // namespace utils

namespace vcu2 {

#ifdef HAVE_VCU2_CTRLSW

void testAllegroDecoderInitRiscV() {
    AL_ERR err = AL_Lib_Decoder_Init(AL_LIB_DECODER_ARCH_RISCV);
    (void) err;
    // Optionally handle the error, e.g.:
    // if (err != AL_SUCCESS) { /* handle error */ }
}

#else

// When VCU2 is not available, these functions are already inlined in the header

#endif // HAVE_VCU2_CTRLSW

} // namespace vcu2
} // namespace vcucodec
} // namespace cv