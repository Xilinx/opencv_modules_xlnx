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

// Implementation of private utility functions

}  // namespace utils

namespace vcu2 {

#ifdef HAVE_VCU2_CTRLSW

void testAllegroDecoderInitRiscV() {
    AL_ERR err = AL_Lib_Decoder_Init(AL_LIB_DECODER_ARCH_RISCV);
    // Optionally handle the error, e.g.:
    // if (err != AL_SUCCESS) { /* handle error */ }
}

#else

// When VCU2 is not available, these functions are already inlined in the header

#endif // HAVE_VCU2_CTRLSW

} // namespace vcu2
} // namespace vcucodec
} // namespace cv