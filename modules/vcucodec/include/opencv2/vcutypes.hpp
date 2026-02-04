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

#ifndef OPENCV_VCUTYPES_HPP
#define OPENCV_VCUTYPES_HPP

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace cv {
namespace vcucodec {

/** @defgroup vcucodec_types VCU Types
    @ingroup vcucodec
    @brief Type definitions for VCU codec module.
*/

//! @addtogroup vcucodec_types
//! @{

/// Auto-detect format, used where FOURCC is required but unknown, or automatically determined
/// (for which also 'AUTO' or 'NULL' FOURCC codes can be passed).
const int VCU_FOURCC_AUTO = 0;

/// Enum class \ref Codec defines the codec types supported by the VCU codec module.
enum class Codec
{
    AVC  = 0,  ///< AVC/H.264 codec.
    HEVC = 1,  ///< HEVC/H.265 codec.
    JPEG = 2   ///< JPEG only (VCU2 and decode only).
};

/// Enum class \ref PicStruct defines the picture structure of the frames or fields.
enum class PicStruct
{
    FRAME          =  0, ///< Frame picture structure.
    TOP            =  1, ///< Top field.
    BOT            =  2, ///< Bottom field.
    TOP_BOT        =  3, ///< Top and bottom fields.
    BOT_TOP        =  4, ///< Bottom and top fields.
    TOP_BOT_TOP    =  5, ///< Top field followed by bottom field followed by top field.
    BOT_TOP_BOT    =  6, ///< Bottom field followed by top field followed by bottom field.
    FRAME_X2       =  7, ///< Frame picture structure repeated twice.
    FRAME_X3       =  8, ///< Frame picture structure repeated three times.
    TOP_PREV_BOT   =  9, ///< Top field with previous bottom field.
    BOT_PREV_TOP   = 10, ///< Bottom field with previous top field.
    TOP_NEXT_BOT   = 11, ///< Top field with next bottom field.
    BOT_NEXT_TOP   = 12, ///< Bottom field with next top field.
};

/// Enum class \ref BitDepth defines which bit depth to use for the frames.
/// Note that truncation of bit depth is not supported; for example, if the stream has 10, or 12
/// bits per component, it will not truncate to 8. It will pad 8, or 10 to 12 bits per component
/// when specified.
/// Note in raster format, for 10 and 12 bits components, the value is padded with 0s to 16 bits.
enum class BitDepth
{
    FIRST  =  0, ///< First bit depth found in stream.
    ALLOC  = -1, ///< Use preallocated bitdepth or bitdepth from stream.
    STREAM = -2, ///< Bitdepth of decoded frame.
    B8     =  8, ///< 8 bits per component.
    B10    = 10, ///< 10 bits per component.
    B12    = 12  ///< 12 bits per component.
};

/// Enum class \ref Tier defines the tier for encoding.
enum class Tier {
    MAIN = 0,  ///< Use Main Tier profile.
    HIGH = 1   ///< Use High Tier profile.
};

////////////////////////////////////////////////////////////////////////////////////////////////////
//  RATE CONTROL

/// Enum class \ref RCMode defines the Rate Control Mode to use for encoding.
enum class RCMode
{
    CONST_QP    = 0, ///< Constant QP.
    CBR         = 1, ///< Constant bitrate.
    VBR         = 2, ///< Variable bitrate.
    LOW_LATENCY = 3, ///< Low latency mode.
    CAPPED_VBR  = 4  ///< Capped variable bitrate.
};

/// Enum class \ref Entropy specifies which entropy coding to use, CAVLC or CABAC.
enum class Entropy
{
    CAVLC, ///< Context-based Adaptive Variable Length Coding.
    CABAC  ///< Context-based Adaptive Binary Arithmetic Coding.
};

////////////////////////////////////////////////////////////////////////////////////////////////////
//  GROUP OF PICTURES

/// Enum class \ref GOPMode specifies the structure of the Group Of Pictures.
enum class GOPMode
{
    BASIC        = 2, ///< (default) IBBPBBP… or IPPPPP….
    BASIC_B      = 3, ///< Like basic, using B-frame references instead of P-frames.
    PYRAMIDAL    = 4, ///< B frames are used as reference by more B frames: IbbBbbP….
    PYRAMIDAL_B  = 5, ///< Like pyramidal, using B-frame references instead of P-frames.
    LOW_DELAY_P  = 8, ///< I picture followed by P-frames only, referencing only previous frame.
    LOW_DELAY_B  = 9, ///< I picture followed by B-frames only, referencing only previous frame.
    ADAPTIVE     = 16 ///< Use an adaptive number of consecutive B-frames.
};

/// Enum class \ref GDRMode specifies the decoder refresh scheme to use.
enum class GDRMode
{
    DISABLE    = 0, ///< No Gradual %Decoder Refresh.
    VERTICAL   = 2, ///< Vertical Gradual %Decoder Refresh.
    HORIZONTAL = 3  ///< Horizontal Gradual %Decoder Refresh.
};

////////////////////////////////////////////////////////////////////////////////////////////////////
//  HDR SEI

/// Struct \ref ChromaCoordinates holds CIE 1931 xy chromaticity coordinates.
struct CV_EXPORTS_W_SIMPLE ChromaCoordinates
{
    CV_PROP_RW int x; ///< x chromaticity coordinate scaled by 50000.
    CV_PROP_RW int y; ///< y chromaticity coordinate scaled by 50000.
};

/// Struct \ref MasteringDisplayColourVolume contains the Mastering Display Colour Volume SEI information.
struct CV_EXPORTS_W_SIMPLE MasteringDisplayColourVolume
{
    CV_PROP_RW std::vector<ChromaCoordinates> display_primaries; ///< Display primaries as (x,y) for R, G, B.
    CV_PROP_RW ChromaCoordinates white_point;         ///< White point as (x,y).
    CV_PROP_RW int max_display_mastering_luminance;   ///< Max display mastering luminance in cd/m^2.
    CV_PROP_RW int min_display_mastering_luminance;   ///< Min display mastering luminance in cd/m^2.
};

/// Struct \ref ContentLightLevel contains the Content Light Level SEI information.
struct CV_EXPORTS_W_SIMPLE ContentLightLevel
{
    CV_PROP_RW int max_content_light_level;       ///< Max content light level in cd/m^2.
    CV_PROP_RW int max_pic_average_light_level;   ///< Max picture average light level in cd/m^2.
};

/// Struct \ref AlternativeTransferCharacteristics contains the Alternative Transfer Characteristics
/// SEI information.
struct CV_EXPORTS_W_SIMPLE AlternativeTransferCharacteristics
{
    CV_PROP_RW int preferred_transfer_characteristics; ///< Preferred transfer characteristics.
};

/// Struct \ref ProcessingWindow_ST2094_10 defines the active area offsets for ST 2094-10 metadata.
struct CV_EXPORTS_W_SIMPLE ProcessingWindow_ST2094_10
{
    CV_PROP_RW int active_area_left_offset;   ///< Left offset of the active area in pixels.
    CV_PROP_RW int active_area_right_offset;  ///< Right offset of the active area in pixels.
    CV_PROP_RW int active_area_top_offset;    ///< Top offset of the active area in pixels.
    CV_PROP_RW int active_area_bottom_offset; ///< Bottom offset of the active area in pixels.
};

/// Struct \ref ImageCharacteristics_ST2094_10 contains PQ-encoded luminance characteristics.
struct CV_EXPORTS_W_SIMPLE ImageCharacteristics_ST2094_10
{
    CV_PROP_RW int min_pq; ///< Minimum PQ-encoded luminance value.
    CV_PROP_RW int max_pq; ///< Maximum PQ-encoded luminance value.
    CV_PROP_RW int avg_pq; ///< Average PQ-encoded luminance value.
};

/// Struct \ref ManualAdjustment_ST2094_10 contains trim parameters for display mapping.
struct CV_EXPORTS_W_SIMPLE ManualAdjustment_ST2094_10
{
    CV_PROP_RW int target_max_pq;        ///< Target maximum PQ value.
    CV_PROP_RW int trim_slope;           ///< Slope adjustment for tone mapping.
    CV_PROP_RW int trim_offset;          ///< Offset adjustment for tone mapping.
    CV_PROP_RW int trim_power;           ///< Power adjustment for tone mapping.
    CV_PROP_RW int trim_chroma_weight;   ///< Chroma weight adjustment.
    CV_PROP_RW int trim_saturation_gain; ///< Saturation gain adjustment.
    CV_PROP_RW int ms_weight;            ///< Mid-tone saturation weight.
};


/// Struct \ref DynamicMeta_ST2094_10 contains the Dynamic Metadata ST 2094-10 SEI information.
struct CV_EXPORTS_W_SIMPLE DynamicMeta_ST2094_10
{
    CV_PROP_RW int application_version;  ///< Application version, should be 0.
    CV_PROP_RW bool processing_window_flag; ///< True if processing window is present.
    CV_PROP_RW ProcessingWindow_ST2094_10 processing_window; ///< Processing window definition.
    CV_PROP_RW ImageCharacteristics_ST2094_10 image_characteristics; ///< Image luminance characteristics.
    CV_PROP_RW std::vector<ManualAdjustment_ST2094_10> manual_adjustments; ///< Manual adjustment parameters per target display.
};

/// Struct \ref ProcessingWindow_ST2094_1 defines a rectangular processing window.
struct CV_EXPORTS_W_SIMPLE ProcessingWindow_ST2094_1
{
    CV_PROP_RW int upper_left_corner_x;  ///< X coordinate of upper left corner.
    CV_PROP_RW int upper_left_corner_y;  ///< Y coordinate of upper left corner.
    CV_PROP_RW int lower_right_corner_x; ///< X coordinate of lower right corner.
    CV_PROP_RW int lower_right_corner_y; ///< Y coordinate of lower right corner.
};

/// Struct \ref ProcessingWindow_ST2094_40 defines an elliptical processing window for ST 2094-40.
struct CV_EXPORTS_W_SIMPLE ProcessingWindow_ST2094_40
{
    CV_PROP_RW ProcessingWindow_ST2094_1 base_processing_window; ///< Base rectangular window.
    CV_PROP_RW int center_of_ellipse_x;             ///< X coordinate of ellipse center.
    CV_PROP_RW int center_of_ellipse_y;             ///< Y coordinate of ellipse center.
    CV_PROP_RW int rotation_angle;                  ///< Rotation angle of the ellipse in degrees.
    CV_PROP_RW int semimajor_axis_internal_ellipse; ///< Semimajor axis of internal ellipse.
    CV_PROP_RW int semimajor_axis_external_ellipse; ///< Semimajor axis of external ellipse.
    CV_PROP_RW int semiminor_axis_external_ellipse; ///< Semiminor axis of external ellipse.
    CV_PROP_RW int overlap_process_option;          ///< Overlap processing option.
};

/// Struct \ref DisplayPeakLuminance_ST2094_40 contains peak luminance distribution data.
struct CV_EXPORTS_W_SIMPLE DisplayPeakLuminance_ST2094_40
{
    CV_PROP_RW bool actual_peak_luminance_flag;        ///< True if actual peak luminance data is present.
    CV_PROP_RW int num_rows_actual_peak_luminance;     ///< Number of rows in the peak luminance array.
    CV_PROP_RW int num_cols_actual_peak_luminance;     ///< Number of columns in the peak luminance array.
    CV_PROP_RW std::vector<std::vector<int>> actual_peak_luminance; ///< 2D array of peak luminance values.
};

/// Struct \ref TargetedSystemDisplay_ST2094_40 describes the target display characteristics.
struct CV_EXPORTS_W_SIMPLE TargetedSystemDisplay_ST2094_40
{
    CV_PROP_RW uint32_t maximum_luminance;                ///< Maximum luminance of the target display in cd/m^2.
    CV_PROP_RW DisplayPeakLuminance_ST2094_40 peak_luminance; ///< Peak luminance distribution.
};

/// Struct \ref ToneMapping_ST2094_40 contains tone mapping curve parameters.
struct CV_EXPORTS_W_SIMPLE ToneMapping_ST2094_40
{
    CV_PROP_RW bool tone_mapping_flag;              ///< True if tone mapping data is present.
    CV_PROP_RW int knee_point_x;                    ///< X coordinate of the knee point.
    CV_PROP_RW int knee_point_y;                    ///< Y coordinate of the knee point.
    CV_PROP_RW std::vector<int> bezier_curve_anchors; ///< Bezier curve anchor points.
};

/// Struct \ref ProcessingWindowTransform_ST2094_40 contains per-window HDR transform parameters.
struct CV_EXPORTS_W_SIMPLE ProcessingWindowTransform_ST2094_40
{
    CV_PROP_RW std::vector<int> maxscl;                        ///< Maximum content light level per color component.
    CV_PROP_RW int average_maxrgb;                             ///< Average maximum RGB value.
    CV_PROP_RW std::vector<int> distribution_maxrgb_percentages; ///< Percentages for maxRGB distribution.
    CV_PROP_RW std::vector<int> distribution_maxrgb_percentiles; ///< Percentile values for maxRGB distribution.
    CV_PROP_RW int fraction_bright_pixels;                     ///< Fraction of bright pixels.
    CV_PROP_RW ToneMapping_ST2094_40 tone_mapping;             ///< Tone mapping parameters.
    CV_PROP_RW bool color_saturation_mapping_flag;             ///< True if color saturation mapping is enabled.
    CV_PROP_RW int color_saturation_weight;                    ///< Color saturation weight.
};


/// Struct \ref DynamicMeta_ST2094_40 contains the Dynamic Metadata ST 2094-40 SEI information.
struct CV_EXPORTS_W_SIMPLE DynamicMeta_ST2094_40
{
    CV_PROP_RW int application_version; ///< Application version.
    CV_PROP_RW std::vector<ProcessingWindow_ST2094_40> processing_windows; ///< Processing windows.
    CV_PROP_RW TargetedSystemDisplay_ST2094_40 targeted_system_display; ///< Target display characteristics.
    CV_PROP_RW DisplayPeakLuminance_ST2094_40 mastering_display_peak_luminance; ///< Mastering display peak luminance.
    CV_PROP_RW std::vector<ProcessingWindowTransform_ST2094_40> processing_window_transforms; ///< Per-window transform parameters.
};

/// Struct \ref HDRSEIs contains the HDR SEI information to insert in the stream.
struct CV_EXPORTS_W_SIMPLE HDRSEIs
{
    CV_PROP_RW bool hasMDCV;           ///< True if mdcv contains valid data.
    CV_PROP_RW MasteringDisplayColourVolume mdcv; ///< Mastering Display Colour Volume SEI.
    CV_PROP_RW bool hasCLL;            ///< True if cll contains valid data.
    CV_PROP_RW ContentLightLevel cll;  ///< Content Light Level SEI.
    CV_PROP_RW bool hasATC;            ///< True if atc contains valid data.
    CV_PROP_RW AlternativeTransferCharacteristics atc; ///< Alternative Transfer Characteristics.
    CV_PROP_RW bool hasST2094_10;      ///< True if st2094_10 contains valid data.
    CV_PROP_RW DynamicMeta_ST2094_10 st2094_10; ///< Dynamic Metadata ST 2094-10 SEI.
    CV_PROP_RW bool hasST2094_40;      ///< True if st2094_40 contains valid data.
    CV_PROP_RW DynamicMeta_ST2094_40 st2094_40; ///< Dynamic Metadata ST 2094-40 SEI.
};

//! @cond INTERNAL
template<typename T>
String toString(const T& value);

template<typename T>
String toString(const std::vector<T>& vec)
{
    std::string result = "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) result += ",";
        result += toString(vec[i]);
    }
    result += "]";
    return result;
}
//! @endcond

//! @}
}  // namespace vcucodec
}  // namespace cv

#endif // OPENCV_VCUTYPES_HPP
