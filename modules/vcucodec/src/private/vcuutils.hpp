#ifndef OPENCV_VCUCODEC_VCUUTILS_HPP
#define OPENCV_VCUCODEC_VCUUTILS_HPP

#include "opencv2/core.hpp"

#ifdef HAVE_VCU2_CTRLSW
// Include VCU2 Control Software headers
#include "lib_common/BufferAPI.h"
#include "lib_common/Profiles.h"
#include "lib_common/SliceConsts.h"
#include "lib_common/RoundUp.h"

// Forward declarations for VCU2 types
typedef struct AL_TDecoder AL_TDecoder;
typedef struct AL_TEncoder AL_TEncoder;
typedef struct AL_TBuffer AL_TBuffer;

namespace cv {
namespace vcucodec {
namespace vcu2 {


} // namespace vcu2
} // namespace vcucodec
} // namespace cv

#else
// Stub definitions when VCU2 is not available
namespace cv {
namespace vcucodec {
namespace vcu2 {

inline bool isVCU2Available() { return false; }


} // namespace vcu2
} // namespace vcucodec
} // namespace cv

#endif // HAVE_VCU2_CTRLSW

#endif // OPENCV_VCUCODEC_VCUUTILS_HPP