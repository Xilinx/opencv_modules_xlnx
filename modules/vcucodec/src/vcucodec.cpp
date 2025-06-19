#include "opencv2/vcucodec.hpp"
#include <map>

namespace cv {
namespace vcucodec {

// Mapping between VCUFourCC enum values and their corresponding FourCC strings
static const std::map<VCUFourCC, String> fourccMap = {
    {VCU_I0AL, "I0AL"},
    {VCU_I0CL, "I0CL"},
    {VCU_I2AL, "I2AL"},
    {VCU_I2CL, "I2CL"},
    {VCU_I420, "I420"},
    {VCU_I422, "I422"},
    {VCU_I444, "I444"},
    {VCU_I4AL, "I4AL"},
    {VCU_I4CL, "I4CL"},
    {VCU_IYUV, "IYUV"},
    {VCU_NV12, "NV12"},
    {VCU_NV16, "NV16"},
    {VCU_NV24, "NV24"},
    {VCU_P010, "P010"},
    {VCU_P012, "P012"},
    {VCU_P016, "P016"},
    {VCU_P210, "P210"},
    {VCU_P212, "P212"},
    {VCU_P216, "P216"},
    {VCU_P410, "P410"},
    {VCU_Y010, "Y010"},
    {VCU_Y012, "Y012"},
    {VCU_I4AM, "I4AM"},
    {VCU_Y800, "Y800"},
    {VCU_YUVP, "YUVP"},
    {VCU_YUY2, "YUY2"},
    {VCU_YV12, "YV12"},
    {VCU_YV16, "YV16"},
    {VCU_T508, "T508"},
    {VCU_T50A, "T50A"},
    {VCU_T50C, "T50C"},
    {VCU_T528, "T528"},
    {VCU_T52A, "T52A"},
    {VCU_T52C, "T52C"},
    {VCU_T548, "T548"},
    {VCU_T54A, "T54A"},
    {VCU_T54C, "T54C"},
    {VCU_T5M8, "T5M8"},
    {VCU_T5MA, "T5MA"},
    {VCU_T5MC, "T5MC"},
    {VCU_T608, "T608"},
    {VCU_T60A, "T60A"},
    {VCU_T60C, "T60C"},
    {VCU_T628, "T628"},
    {VCU_T62A, "T62A"},
    {VCU_T62C, "T62C"},
    {VCU_T648, "T648"},
    {VCU_T64A, "T64A"},
    {VCU_T64C, "T64C"},
    {VCU_T6M8, "T6M8"},
    {VCU_T6MA, "T6MA"},
    {VCU_T6MC, "T6MC"},
    {VCU_AUTO, "AUTO"}
};

// Function fourccToString converts a FourCC enum value to a string representation.
String fourccToString(VCUFourCC fourcc) {
    auto it = fourccMap.find(fourcc);
    if (it != fourccMap.end()) {
        return it->second;
    }
    return "UNKN";  // Unknown format
}

// Function stringToFourcc converts a string representation to a FourCC enum value.
VCUFourCC stringToFourcc(const String& str) {
    for (const auto& pair : fourccMap) {
        if (pair.second == str) {
            return pair.first;
        }
    }
    throw std::invalid_argument("Unknown FourCC string: " + str);
}

// Placeholder - actual implementations are in vcudec.cpp and vcuenc.cpp

}  // namespace vcucodec
}  // namespace cv 