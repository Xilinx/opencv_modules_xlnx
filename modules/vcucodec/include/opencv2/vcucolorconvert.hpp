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

#ifndef OPENCV_VCUCOLORCONVERT_HPP
#define OPENCV_VCUCOLORCONVERT_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <algorithm>

/** @addtogroup vcucodec
    @{
    @defgroup vcucodec_colorconvert Color Conversion
    @brief Extensible, framework-agnostic color conversion.

    The @ref Surface struct describes a pixel buffer (format, planes, strides,
    dimensions, colour-science parameters).  The @ref ColorConverter interface
    converts between two surfaces.

    Converters are registered and looked up by (srcFourcc, dstFourcc) pair.
    Built-in converters handle:
    - NV12 (8-bit 4:2:0 semi-planar) → BGR / BGRA
    - GRAY → BGR / BGRA

    Additional converters (P010, P012, NV16, P210, P212, etc.) can be
    registered at runtime via @ref registerColorConverter.

    @}
*/

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Converter Interface

/// @brief Abstract base class for a color converter.
///
/// Implementations perform a specific type of conversion identified by
/// the (srcFourcc, dstFourcc) pair under which they are registered.
/// The converter reads all necessary parameters (planes, strides,
/// dimensions, matrix, range) from the @ref ColorConverter::Surface arguments.
///
/// When source and destination dimensions differ, the converter processes
/// min(src.width, dst.width) × min(src.height, dst.height) pixels.
/// This provides natural clipping when a cropped source is placed into a
/// destination region that extends beyond the buffer boundary.
class ColorConverter
{
public:
    ////////////////////////////////////////////////////////////////////////////////////////
    //  Surface

    /// @brief Describes a pixel buffer with format, layout, and colour-science metadata.
    ///
    /// A Surface is a lightweight, non-owning descriptor — it does not allocate or
    /// free any memory.  The caller is responsible for the lifetime of the plane
    /// buffers.
    ///
    /// Plane layout follows the fourcc:
    /// - Semi-planar (NV12, P010, NV16, P210, …):  plane[0] = Y, plane[1] = UV, plane[2] unused.
    /// - Planar (I420, I422, …):                    plane[0] = Y, plane[1] = U, plane[2] = V.
    /// - Packed BGR/BGRA:                           plane[0] = interleaved data, rest unused.
    /// - Gray:                                      plane[0] = Y, rest unused.
    ///
    /// Steps are in bytes.  Unused planes must be nullptr with step 0.
    ///
    /// The @c matrix field uses ITU-T H.273 / ISO 23091-4 matrix_coefficients
    /// values directly (e.g. 1 = BT.709, 5 = BT.601-625, 9 = BT.2020).
    struct Surface
    {
        int       fourcc     = 0;       ///< Pixel format as FOURCC code.
        uint8_t*  plane[3]   = {};      ///< Plane pointers (up to 3). Unused planes nullptr.
        size_t    step[3]    = {};      ///< Per-plane stride in bytes.
        int       width      = 0;       ///< Image width in pixels.
        int       height     = 0;       ///< Image height in pixels.

        /// YCbCr ↔ RGB matrix coefficients (ITU-T H.273 matrix_coefficients).
        /// Common values: 1 = BT.709, 5/6 = BT.601, 9 = BT.2020, 2 = unspecified.
        int       matrix     = 2;       ///< Default: unspecified.

        /// Signal range.  When true, luma uses [0, 2^N-1] and chroma uses
        /// [0, 2^N-1] centered at 2^(N-1).  When false (limited / "video" range),
        /// luma uses [16·S, 235·S] and chroma uses [16·S, 240·S] where S = 2^(N-8).
        bool      fullRange  = false;

        /// @brief Return a sub-region with adjusted plane pointers.
        ///
        /// Creates a new Surface that refers to the rectangle (x, y, w, h) within
        /// this surface.  Plane pointers are offset to the top-left corner of the
        /// crop region, correctly accounting for chroma subsampling (e.g. for NV12,
        /// the UV plane offset is (y/2 * uvStep + x) since chroma is subsampled
        /// 2× in both axes).
        ///
        /// The crop rectangle is clamped to the surface dimensions: if x + w
        /// exceeds width, w is reduced accordingly (likewise for height).
        ///
        /// @param x  Horizontal offset in pixels (must be even for subsampled formats).
        /// @param y  Vertical offset in pixels (must be even for subsampled formats).
        /// @param w  Width of the crop region in pixels (0 = remaining width from x).
        /// @param h  Height of the crop region in pixels (0 = remaining height from y).
        /// @return   A new Surface with adjusted pointers and dimensions.
        Surface crop(int x, int y, int w = 0, int h = 0) const;
    };

    ////////////////////////////////////////////////////////////////////////////////////////

    virtual ~ColorConverter() = default;

    /// @brief Perform the color conversion.
    /// @param src Source surface (pixel data + colour-science metadata).
    /// @param dst Destination surface (pre-allocated planes + desired format/metadata).
    virtual void convert(const Surface& src, Surface& dst) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////
    //  Registry

    /// @brief Register a converter for a source/destination fourcc pair.
    ///
    /// The same converter instance may be registered under multiple fourcc pairs.
    /// If a converter is already registered for the given pair, it is replaced
    /// (last registration wins).  Built-in converters are registered at module
    /// load time.
    ///
    /// @param srcFourcc  Source pixel format as FOURCC code.
    /// @param dstFourcc  Destination pixel format as FOURCC code.
    /// @param converter  Shared pointer to the converter instance.
    static void add(int srcFourcc, int dstFourcc,
                    const std::shared_ptr<ColorConverter>& converter);

    /// @brief Find a converter registered for the given fourcc pair.
    ///
    /// Performs an exact match on (srcFourcc, dstFourcc).
    ///
    /// @param srcFourcc Source pixel format as FOURCC code.
    /// @param dstFourcc Destination pixel format as FOURCC code.
    /// @return Pointer to the registered converter, or nullptr if none found.
    static std::shared_ptr<ColorConverter> find(int srcFourcc, int dstFourcc);
};

#endif // OPENCV_VCUCOLORCONVERT_HPP
