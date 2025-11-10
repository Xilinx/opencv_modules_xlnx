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
//ENUMASSERT(17, AL_GOP_MODE_MAX_ENUM);

// AL_EGdrMode
ENUMASSERT(GDRMode::DISABLE, AL_GDR_OFF);
ENUMASSERT(GDRMode::VERTICAL, AL_GDR_VERTICAL);
ENUMASSERT(GDRMode::HORIZONTAL, AL_GDR_HORIZONTAL);
ENUMASSERT(4, AL_GDR_MAX_ENUM);

template<typename T, std::size_t N>
std::string_view enumToString(const T& value, const char* (&strarray)[N])
{
    int index = static_cast<int>(value);
    if (index < 0 || index >= static_cast<int>(N))
        return "????";
    return strarray[index];
}

// Basic type toString overloads
template<> String toString(const int& value) { return std::to_string(value); }
template<> String toString(const uint32_t& value) { return std::to_string(value); }
template<> String toString(const bool& value) { return value ? "true" : "false"; }
template<> String toString(const std::string& value) { return value; }

template<typename T>
std::string structToString(const T& value)
{
    return "{" + toString(value) + "}";
}

template<typename T, typename... Args>
std::string structToString(const T& value, const Args&... args)
{
    std::string result = "{" + toString(value);
    ((result += "," + toString(args)), ...);
    result += "}";
    return result;
}

template<>
String toString(const Codec& value)
{
    static const char* strarray[] = {"AVC", "HEVC", "JPEG"};
    return enumToString(value, strarray).data();
}

template<>
String toString(const PicStruct& value)
{
    static const char* strarray[] = {"FRAME", "TOP", "BOT", "TOP_BOT", "BOT_TOP", "TOP_BOT_TOP",
        "BOT_TOP_BOT", "FRAME_X2", "FRAME_X3", "TOP_PREV_BOT", "BOT_PREV_TOP", "TOP_NEXT_BOT",
        "BOT_NEXT_TOP"
    };
    return enumToString(value, strarray).data();
}

template<>
String toString(const BitDepth& value)
{
    switch (value)
    {
        case BitDepth::FIRST:  return "FIRST";
        case BitDepth::ALLOC:  return "ALLOC";
        case BitDepth::STREAM: return "STREAM";
        case BitDepth::B8:     return "8";
        case BitDepth::B10:    return "10";
        case BitDepth::B12:    return "12";
        default:               return "????";
    }
}

template<>
String toString(const Tier& value)
{
    static const char* strarray[] = {"MAIN", "HIGH"};
    return enumToString(value, strarray).data();
}

template<>
String toString(const RCMode& value)
{
    static const char* strarray[] = {"CONST_QP", "CBR", "VBR", "LOW_LATENCY", "CAPPED_VBR"};
    return enumToString(value, strarray).data();
}

template<>
String toString(const Entropy& value)
{
    static const char* strarray[] = {"CAVLC", "CABAC"};
    return enumToString(value, strarray).data();
}

template<>
String toString(const GOPMode& value)
{
    switch (value)
    {
        case GOPMode::BASIC:       return "BASIC";
        case GOPMode::BASIC_B:     return "BASIC_B";
        case GOPMode::PYRAMIDAL:   return "PYRAMIDAL";
        case GOPMode::PYRAMIDAL_B: return "PYRAMIDAL_B";
        case GOPMode::LOW_DELAY_P: return "LOW_DELAY_P";
        case GOPMode::LOW_DELAY_B: return "LOW_DELAY_B";
        case GOPMode::ADAPTIVE:    return "ADAPTIVE";
        default:                   return "????";
    }
}

template<>
String toString(const GDRMode& value)
{
    static const char* strarray[] = {"DISABLE", "????", "VERTICAL", "HORIZONTAL"};
    return enumToString(value, strarray).data();
}

template<> String toString(const ChromaCoordinates& value)
{
    return structToString(value.x, value.y);
}

template<> String toString(const MasteringDisplayColourVolume& value)
{
    return structToString(value.display_primaries, value.white_point,
            value.max_display_mastering_luminance, value.min_display_mastering_luminance);
}

template<> String toString(const ContentLightLevel& value)
{
    return structToString(value.max_content_light_level, value.max_pic_average_light_level);
}

template<> String toString(const AlternativeTransferCharacteristics& value)
{
    return structToString(value.preferred_transfer_characteristics);
}

template<> String toString(const ProcessingWindow_ST2094_10& value)
{
    return structToString(value.active_area_left_offset, value.active_area_right_offset,
            value.active_area_top_offset, value.active_area_bottom_offset);
}

template<> String toString(const ImageCharacteristics_ST2094_10& value)
{
    return structToString(value.min_pq, value.max_pq, value.avg_pq);
}

template<> String toString(const ManualAdjustment_ST2094_10& value)
{
    return structToString(value.target_max_pq, value.trim_slope, value.trim_offset, value.trim_power,
            value.trim_chroma_weight, value.trim_saturation_gain, value.ms_weight);
}

template<> String toString(const DynamicMeta_ST2094_10& value)
{
    return structToString(value.application_version, value.processing_window_flag,
            value.processing_window, value.image_characteristics, value.manual_adjustments);
}

template<> String toString(const ProcessingWindow_ST2094_1& value)
{
    return structToString(value.upper_left_corner_x, value.upper_left_corner_y,
            value.lower_right_corner_x, value.lower_right_corner_y);
}

template<> String toString(const ProcessingWindow_ST2094_40& value)
{
    return structToString(value.base_processing_window, value.center_of_ellipse_x,
            value.center_of_ellipse_y, value.rotation_angle, value.semimajor_axis_internal_ellipse,
            value.semimajor_axis_external_ellipse, value.semiminor_axis_external_ellipse,
            value.overlap_process_option);
}

template<> String toString(const DisplayPeakLuminance_ST2094_40& value)
{
    return structToString(value.actual_peak_luminance_flag, value.num_rows_actual_peak_luminance,
            value.num_cols_actual_peak_luminance, value.actual_peak_luminance);
}

template<> String toString(const TargetedSystemDisplay_ST2094_40& value)
{
    return structToString(value.maximum_luminance, value.peak_luminance);
}

template<> String toString(const ToneMapping_ST2094_40& value)
{
    return structToString(value.tone_mapping_flag, value.knee_point_x, value.knee_point_y,
            value.bezier_curve_anchors);
}

template<> String toString(const ProcessingWindowTransform_ST2094_40& value)
{
    return structToString(value.maxscl, value.average_maxrgb,
            value.distribution_maxrgb_percentages, value.distribution_maxrgb_percentiles,
            value.fraction_bright_pixels, value.tone_mapping, value.color_saturation_mapping_flag,
            value.color_saturation_weight);
}

template<> String toString(const DynamicMeta_ST2094_40& value)
{
    return structToString(value.application_version, value.processing_windows,
            value.targeted_system_display, value.mastering_display_peak_luminance,
            value.processing_window_transforms);
}

template<> String toString(const HDRSEIs& value)
{
    std::string result = "{";
    bool needComma = false;
    if (value.hasMDCV) {
        result += "mdcv:" + toString(value.mdcv);
        needComma = true;
    }
    if (value.hasCLL) {
        if (needComma) result += ',';
        result += "cll:" + toString(value.cll);
        needComma = true;
    }
    if (value.hasATC) {
        if (needComma) result += ',';
        result += "atc:" + toString(value.atc);
        needComma = true;
    }
    if (value.hasST2094_10) {
        if (needComma) result += ',';
        result += "st2094_10:" + toString(value.st2094_10);
        needComma = true;
    }
    if (value.hasST2094_40) {
        if (needComma) result += ',';
        result += "st2094_40:" + toString(value.st2094_40);
        needComma = true;
    }
    result += "}";
    return result;
}

}  // namespace vcucodec
}  // namespace cv