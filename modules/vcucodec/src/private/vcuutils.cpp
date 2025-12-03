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
#include "lib_common/HDR.h"
}
#endif

#include <map>

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
        lhs.posX == rhs.posX &&
        lhs.posY == rhs.posY &&
        lhs.cropTop == rhs.cropTop &&
        lhs.cropBottom == rhs.cropBottom &&
        lhs.cropLeft == rhs.cropLeft &&
        lhs.cropRight == rhs.cropRight;
    return equal;
}

bool operator!=(const RawInfo& lhs, const RawInfo& rhs)
{
    return !(lhs == rhs);
}

OutputStream::OutputStream(const String& filename, bool binary)
{
    file_.open(filename, binary ? std::ios::out | std::ios::binary : std::ios::out);
    file_.exceptions(std::ofstream::badbit);
    if (!file_.is_open())
    {
        CV_Error(cv::Error::StsBadArg, "Failed to set output file path '" + filename + "'");
    }
}

OutputStream::~OutputStream()
{
    file_.close();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Template specializations for convert function
////////////////////////////////////////////////////////////////////////////////////////////////////

template <> void convert(ChromaCoordinates& to, const AL_TChromaCoordinates& from)
{
    to.x = from.x;
    to.y = from.y;
}

template <> void convert(MasteringDisplayColourVolume& to, const AL_TMasteringDisplayColourVolume& from)
{
    to.display_primaries.resize(3);
    for (int i = 0; i < 3; i++)
    {
        convert(to.display_primaries[i], from.display_primaries[i]);
    }
    convert(to.white_point, from.white_point);
    to.max_display_mastering_luminance = static_cast<int>(from.max_display_mastering_luminance);
    to.min_display_mastering_luminance = static_cast<int>(from.min_display_mastering_luminance);
}

template <> void convert(ContentLightLevel& to, const AL_TContentLightLevel& from)
{
    to.max_content_light_level = static_cast<int>(from.max_content_light_level);
    to.max_pic_average_light_level = static_cast<int>(from.max_pic_average_light_level);
}

template <> void convert(AlternativeTransferCharacteristics& to, const AL_TAlternativeTransferCharacteristics& from)
{
    to.preferred_transfer_characteristics = static_cast<int>(from.preferred_transfer_characteristics);
}

template <> void convert(ProcessingWindow_ST2094_10& to, const AL_TProcessingWindow_ST2094_10& from)
{
    to.active_area_left_offset = static_cast<int>(from.active_area_left_offset);
    to.active_area_right_offset = static_cast<int>(from.active_area_right_offset);
    to.active_area_top_offset = static_cast<int>(from.active_area_top_offset);
    to.active_area_bottom_offset = static_cast<int>(from.active_area_bottom_offset);
}
template <> void convert(ImageCharacteristics_ST2094_10& to, const AL_TImageCharacteristics_ST2094_10& from)
{
    to.min_pq = static_cast<int>(from.min_pq);
    to.max_pq = static_cast<int>(from.max_pq);
    to.avg_pq = static_cast<int>(from.avg_pq);
}

template <> void convert(ManualAdjustment_ST2094_10& to, const AL_TManualAdjustment_ST2094_10& from)
{
    to.target_max_pq = static_cast<int>(from.target_max_pq);
    to.trim_slope = static_cast<int>(from.trim_slope);
    to.trim_offset = static_cast<int>(from.trim_offset);
    to.trim_power = static_cast<int>(from.trim_power);
    to.trim_chroma_weight = static_cast<int>(from.trim_chroma_weight);
    to.trim_saturation_gain = static_cast<int>(from.trim_saturation_gain);
    to.ms_weight = static_cast<int>(from.ms_weight);
}

template <> void convert(DynamicMeta_ST2094_10& to, const AL_TDynamicMeta_ST2094_10& from)
{
    to.application_version = static_cast<int>(from.application_version);
    to.processing_window_flag = from.processing_window_flag;
    if (to.processing_window_flag)
        convert(to.processing_window, from.processing_window);

    convert(to.image_characteristics, from.image_characteristics);
    to.manual_adjustments.resize(from.num_manual_adjustments);
    for (int i = 0; i < from.num_manual_adjustments; i++)
    {
        convert(to.manual_adjustments[i], from.manual_adjustments[i]);
    }
}

template <> void convert(ProcessingWindow_ST2094_1& to, const AL_TProcessingWindow_ST2094_1& from)
{
    to.upper_left_corner_x = static_cast<int>(from.upper_left_corner_x);
    to.upper_left_corner_y = static_cast<int>(from.upper_left_corner_y);
    to.lower_right_corner_x = static_cast<int>(from.lower_right_corner_x);
    to.lower_right_corner_y = static_cast<int>(from.lower_right_corner_y);
}


template <> void convert(ProcessingWindow_ST2094_40& to, const AL_TProcessingWindow_ST2094_40& from)
{
    convert(to.base_processing_window, from.base_processing_window);
    to.center_of_ellipse_x = static_cast<int>(from.center_of_ellipse_x);
    to.center_of_ellipse_y = static_cast<int>(from.center_of_ellipse_y);
    to.rotation_angle = static_cast<int>(from.rotation_angle);
    to.semimajor_axis_internal_ellipse = static_cast<int>(from.semimajor_axis_internal_ellipse);
    to.semimajor_axis_external_ellipse = static_cast<int>(from.semimajor_axis_external_ellipse);
    to.semiminor_axis_external_ellipse = static_cast<int>(from.semiminor_axis_external_ellipse);
    to.overlap_process_option = static_cast<int>(from.overlap_process_option);
}

template <> void convert(DisplayPeakLuminance_ST2094_40& to,
                        const AL_TDisplayPeakLuminance_ST2094_40& from)
{
    to.actual_peak_luminance_flag = from.actual_peak_luminance_flag;
    to.num_rows_actual_peak_luminance = static_cast<int>(from.num_rows_actual_peak_luminance);
    to.num_cols_actual_peak_luminance = static_cast<int>(from.num_cols_actual_peak_luminance);
    to.actual_peak_luminance.resize(to.num_rows_actual_peak_luminance);
    for (int i = 0; i < to.num_rows_actual_peak_luminance; i++)
    {
        to.actual_peak_luminance[i].resize(to.num_cols_actual_peak_luminance);
        for (int j = 0; j < to.num_cols_actual_peak_luminance; j++)
        {
            to.actual_peak_luminance[i][j] = static_cast<int>(from.actual_peak_luminance[i][j]);
        }
    }
}

template <> void convert(TargetedSystemDisplay_ST2094_40& to,
                         const AL_TTargetedSystemDisplay_ST2094_40& from)
{
    to.maximum_luminance = static_cast<int>(from.maximum_luminance);
    convert(to.peak_luminance, from.peak_luminance);
}

template <> void convert(ToneMapping_ST2094_40& to, const AL_TToneMapping_ST2094_40& from)
{
    to.tone_mapping_flag = from.tone_mapping_flag;
    if (to.tone_mapping_flag)
    {
        to.knee_point_x = static_cast<int>(from.knee_point_x);
        to.knee_point_y = static_cast<int>(from.knee_point_y);
        to.bezier_curve_anchors.resize(from.num_bezier_curve_anchors);
        for (int i = 0; i < from.num_bezier_curve_anchors; i++)
        {
            to.bezier_curve_anchors[i] = static_cast<int>(from.bezier_curve_anchors[i]);
        }
    }
}

template <> void convert(ProcessingWindowTransform_ST2094_40& to,
                         const AL_TProcessingWindowTransform_ST2094_40& from)
{
    to.maxscl.resize(3);
    for (int i = 0; i < 3; i++)
    {
        to.maxscl[i] = static_cast<int>(from.maxscl[i]);
    }
    to.average_maxrgb = static_cast<int>(from.average_maxrgb);
    to.distribution_maxrgb_percentages.resize(from.num_distribution_maxrgb_percentiles);
    to.distribution_maxrgb_percentiles.resize(from.num_distribution_maxrgb_percentiles);
    for (int i = 0; i < from.num_distribution_maxrgb_percentiles; i++)
    {
        to.distribution_maxrgb_percentages[i] =
                static_cast<int>(from.distribution_maxrgb_percentages[i]);
        to.distribution_maxrgb_percentiles[i] =
                static_cast<int>(from.distribution_maxrgb_percentiles[i]);
    }
}

template <> void convert(DynamicMeta_ST2094_40& to, const AL_TDynamicMeta_ST2094_40& from)
{
    to.application_version = static_cast<int>(from.application_version);
    to.processing_windows.resize(from.num_windows);
    for (int i = 0; i < from.num_windows; i++)
    {
        convert(to.processing_windows[i], from.processing_windows[i]);
    }
    convert(to.targeted_system_display, from.targeted_system_display);
    convert(to.mastering_display_peak_luminance, from.mastering_display_peak_luminance);
    to.processing_window_transforms.resize(from.num_windows);
    for (int i = 0; i < from.num_windows; i++)
    {
        convert(to.processing_window_transforms[i], from.processing_window_transforms[i]);
    }
}

template <> void convert(HDRSEIs& to, const AL_THDRSEIs& from)
{
    to.hasMDCV = from.bHasMDCV;
    to.hasCLL = from.bHasCLL;
    to.hasATC = from.bHasATC;
    to.hasST2094_10 = from.bHasST2094_10;
    to.hasST2094_40 = from.bHasST2094_40;
    if(to.hasMDCV)
        convert(to.mdcv, from.tMDCV);
    if(to.hasCLL)
        convert(to.cll, from.tCLL);
    if(to.hasATC)
        convert(to.atc, from.tATC);
    if(to.hasST2094_10)
        convert(to.st2094_10, from.tST2094_10);
    if(to.hasST2094_40)
        convert(to.st2094_40, from.tST2094_40);
}

namespace { // anonymous

struct _FormatInfo { int fourcc; bool decodeable; bool encodeable; };
const bool E = true;
const bool D = true;
const bool d = false;
const bool e = false;
std::map<int, _FormatInfo> const formatInfos =
{
    {FOURCC(NULL), {FOURCC(NULL), D, E}},
    {FOURCC(AUTO), {FOURCC(AUTO), D, E}},
    {FOURCC(Y800), {FOURCC(Y800), D, E}},
    {FOURCC(NV12), {FOURCC(NV12), D, E}},
    {FOURCC(I420), {FOURCC(I420), D, e}},
    {FOURCC(P010), {FOURCC(P010), D, E}},
#ifdef HAVE_VCU2_CTRLSW
    {FOURCC(P012), {FOURCC(P012), D, E}},
#endif
    {FOURCC(NV16), {FOURCC(NV16), D, E}},
    {FOURCC(P210), {FOURCC(P210), D, E}},
#ifdef HAVE_VCU2_CTRLSW
    {FOURCC(P212), {FOURCC(P212), D, E}},
    {FOURCC(I444), {FOURCC(I444), D, E}},
    {FOURCC(I4AL), {FOURCC(I4AL), D, E}},
    {FOURCC(I4CL), {FOURCC(I4CL), D, E}},
#endif
};

} // anonymous namespace

FormatInfo::FormatInfo(int fourcc_)
{
    fourcc = (fourcc_ == 0) ? FOURCC(NULL) : fourcc_;
    auto it = formatInfos.find(fourcc_);
    if (it != formatInfos.end())
    {
        _FormatInfo const& fi = it->second;
        decodeable = fi.decodeable;
        encodeable = fi.encodeable;
    }
    else
    {
        decodeable = false;
        encodeable = false;
    }
}

// static functions
String FormatInfo::getFourCCs(bool decoder)
{
    String result;
    for (auto const& [fourcc, fi] : formatInfos) {
        if ((decoder && fi.decodeable) || (!decoder && fi.encodeable)) {
            if (fi.fourcc == FOURCC(NULL) || fi.fourcc == FOURCC(AUTO)) continue;
            if (!result.empty()) result += ", ";
            result += AL_FourCCToString(fourcc).cFourcc;
        }
    }
    return result;
}

} // namespace vcucodec
} // namespace cv
