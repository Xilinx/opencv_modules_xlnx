// SPDX-FileCopyrightText: © 2026 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT
//
// Ported into the OpenCV vcucodec module from the AMD/Allegro VCU2 control-software
// sample encoder (exe_encoder/ROIMngr.cpp). The algorithm that turns a set of
// rectangular regions + qualities into a per-LCU relative QP table lives only in the
// sample application, not in the shipped ctrl-sw library, so it is replicated here.
// The public "AL_RoiMngr_*" / "AL_TRoiMngrCtx" names have been renamed into the
// cv::vcucodec namespace (class RoiManager); the "AL_" prefix is kept only for genuine
// liballegro / lib_common types this manager depends on.

#ifndef OPENCV_VCUCODEC_VCUROIMANAGER_HPP
#define OPENCV_VCUCODEC_VCUROIMANAGER_HPP

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

extern "C" {
#include "config.h"
#include "lib_common_enc/EncBuffers.h"
}

namespace cv {
namespace vcucodec {

/// @brief Overlap priority used when regions intersect (ported from AL_ERoiOrder).
enum class RoiOrder
{
    INCOMING, ///< The earliest-defined region wins.
    QUALITY   ///< The highest-quality (lowest-QP) region wins.
};

/// @brief Quality "codes" understood by RoiManager.
///
/// A quality code is a signed relative QP delta (e.g. -5 = higher quality, +5 = lower),
/// or one of the two force sentinels below. This mirrors the encoding the underlying
/// hardware QP table expects (what the reference called AL_ERoiQuality).
namespace roiquality {
    constexpr int STATIC = MASK_FORCE_MV0;   ///< Force MV0 (region identical to the reference).
    constexpr int INTRA  = MASK_FORCE_INTRA; ///< Force Intra prediction.
}

/// @brief Builds a per-LCU relative QP table from a scheduled set of rectangular regions.
///
/// Replaces the file-driven ROI path of the reference encoder. Regions are registered
/// once with addRegion() and given an activation window with enableRegion()/
/// disableRegion(); fillBuffer(frameIdx, ...) then selects the regions active at that
/// frame, resolves the background and overlap order for that frame, and writes the QP
/// table handed to AL_Encoder_Process(). The class is the single source of truth for the
/// region set and is safe to use from the caller thread (scheduling) and the encode
/// thread (fillBuffer) concurrently.
class RoiManager
{
public:
    /// @param picWidth,picHeight   Encoded picture size in pixels.
    /// @param profile              Encoder profile (selects QP range / AOM handling).
    /// @param log2MaxCuSize        log2 of the max coding-unit (LCU) size.
    /// @param bkgQualityCode       Default background quality code (MEDIUM == 0).
    /// @param order                Default overlap priority.
    RoiManager(int32_t picWidth, int32_t picHeight, AL_EProfile profile,
               uint8_t log2MaxCuSize, int bkgQualityCode, RoiOrder order);
    ~RoiManager();

    RoiManager(const RoiManager&) = delete;
    RoiManager& operator=(const RoiManager&) = delete;

    /// Register a persistent region (raw pixel coordinates). A region starts disabled.
    /// @param background  true for a full-frame background whose quality sets the base QP.
    /// @return an opaque id used with enableRegion()/disableRegion()/isActive().
    int32_t addRegion(int32_t posX, int32_t posY, int32_t width, int32_t height,
                      int qualityCode, bool background);

    /// Activate region @p id from @p frameIdx onwards (open-ended).
    void enableRegion(int32_t id, int32_t frameIdx);

    /// Deactivate region @p id from @p frameIdx onwards.
    void disableRegion(int32_t id, int32_t frameIdx);

    /// Schedule a global overlap-order change effective from @p frameIdx.
    void setOrder(int32_t frameIdx, RoiOrder order);

    /// Is region @p id active at the most recently filled frame?
    bool isActive(int32_t id) const;

    /// Fill @p pQPs with the relative QP table for @p frameIdx. Selects the active
    /// regions and resolves the background/order scheduled for that frame.
    void fillBuffer(int32_t frameIdx, int32_t numQpPerLcu, int32_t numBytesPerLcu,
                    uint8_t* pQPs, int32_t lcuQpOffset);

private:
    /// A user-declared region with its activation window (raw pixel coordinates).
    struct RegionDef
    {
        int32_t id;
        int32_t posX;
        int32_t posY;
        int32_t width;
        int32_t height;
        int     qualityCode;   ///< signed relative QP delta or a force sentinel
        bool    background;
        int32_t enableFrame;   ///< active when frame in [enableFrame, disableFrame)
        int32_t disableFrame;
    };

    /// A region snapped to the LCU grid, used while filling the QP table.
    struct Node
    {
        int32_t iPosX;
        int32_t iPosY;
        int32_t iWidth;
        int32_t iHeight;
        int16_t iDeltaQP;
    };

    Node toNode(const RegionDef& region) const;
    bool activeAt(const RegionDef& region, int32_t frameIdx) const;
    RegionDef* find(int32_t id);

    uint32_t getNodePosInBuf(uint32_t uLcuX, uint32_t uLcuY, int32_t iNumBytesPerLCU) const;
    void meanQuality(uint8_t* pTargetQP, uint8_t* iDQp1, uint8_t iDQp2, int32_t iNumQPPerLCU,
                     int32_t iLcuQpOffset);
    void updateTransitionHorz(uint8_t* pLcu1, uint8_t* pLcu2, int32_t iNumQPPerLCU,
            int32_t iNumBytesPerLCU, int32_t iLcuPicWidth, int32_t iPosX, int32_t iWidth,
            int8_t iQP, int32_t iLcuQpOffset);
    void updateTransitionVert(uint8_t* pLcu1, uint8_t* pLcu2, int32_t iNumQPPerLCU,
            int32_t iNumBytesPerLCU, int32_t iLcuPicWidth, int32_t iHeight, int8_t iQP,
            int32_t iLcuQpOffset);
    void computeROI(int32_t iNumQPPerLCU, int32_t iNumBytesPerLCU, uint8_t* pQPs,
                    int32_t iLcuQpOffset, const Node& node);

    mutable std::mutex mtx_;
    int32_t iPicWidth;
    int32_t iPicHeight;
    int16_t iLcuPicWidth;
    int16_t iLcuPicHeight;
    int32_t iNumLCUs;
    uint8_t uLog2MaxCuSize;
    int8_t  iMinQP;
    int8_t  iMaxQP;
    bool    bIsAOM;
    int      eDefaultBkgQuality;   ///< background quality code when no background is active
    RoiOrder eDefaultOrder;
    std::vector<RegionDef> regions_;
    std::vector<std::pair<int32_t, RoiOrder>> orderChanges_;  ///< scheduled order changes
    int32_t nextId_;
    int32_t currentFrame_;
    int16_t* pDeltaQpSegments;
};

} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUROIMANAGER_HPP
