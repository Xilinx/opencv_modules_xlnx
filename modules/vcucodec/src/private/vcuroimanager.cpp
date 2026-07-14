// SPDX-FileCopyrightText: © 2026 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT
//
// Ported into the OpenCV vcucodec module from the AMD/Allegro VCU2 control-software
// sample encoder (exe_encoder/ROIMngr.cpp) and renamed into cv::vcucodec (class
// RoiManager). Only genuine liballegro / lib_common symbols keep the "AL_" prefix.

#include "vcuroimanager.hpp"

#include <algorithm>
#include <list>
#include <stdexcept>

extern "C" {
#include "lib_common/SliceConsts.h"
#include "lib_common/Round.h"
}

namespace cv {
namespace vcucodec {

namespace { // anonymous

// AOM background segment quality codes (relative QP deltas), used only when bIsAOM.
constexpr int Q_HIGH = -5;
constexpr int Q_MEDIUM = 0;
constexpr int Q_LOW = 5;
constexpr int Q_DONT_CARE = 31;

/***************************************************************************/
inline int32_t clip3(int32_t iVal, int32_t iMin, int32_t iMax)
{
    return (iVal < iMin) ? iMin : (iVal > iMax) ? iMax : iVal;
}

/****************************************************************************/
int32_t extendSign(uint32_t value, int32_t numBits)
{
    value &= 0xffffffff >> (32 - numBits);

    if(value >= (1u << (numBits - 1)))
        value -= 1 << numBits;
    return value;
}

/****************************************************************************/
int8_t toInt(int eQuality)
{
    return extendSign(eQuality, 6);
}

/****************************************************************************/
uint16_t getNewDeltaQP(int eQuality)
{
    if(eQuality == roiquality::STATIC)
        return MASK_FORCE_MV0;

    if(eQuality == roiquality::INTRA)
        return MASK_FORCE_INTRA;

    return toInt(eQuality) & MASK_QP;
}

/****************************************************************************/
int8_t getDQp(uint16_t iDeltaQP)
{
    return (int8_t)((iDeltaQP & MASK_QP) << (8 - MASK_QP_NUMBITS)) >> (8 - MASK_QP_NUMBITS);
}

/****************************************************************************/
uint8_t getSegmentId(int16_t* pDeltaQPSegments, int16_t iDeltaQP)
{
    int8_t iDeltaQPOnly = getDQp(iDeltaQP);

    for(uint8_t i = 0; i < AL_QPTABLE_NUM_SEGMENTS - 1; ++i)
        if(iDeltaQPOnly < pDeltaQPSegments[i + 1])
            return i;

    return AL_QPTABLE_NUM_SEGMENTS - 1;
}

/****************************************************************************/
bool shouldInsertAfter(int16_t iCurrentQP, int16_t iQPToInsert)
{
    if(iQPToInsert & MASK_FORCE_INTRA)
        return true;

    return getDQp(iCurrentQP) > getDQp(iQPToInsert);
}

/****************************************************************************/
inline uint8_t translateSegIDToDltQP(int16_t* pDeltaQpSegments, uint8_t* pLcuBuf, bool bIsAOM,
                                     int32_t iLcuQpOffset)
{
    if(!bIsAOM)
        return *pLcuBuf;

    /* Skip the first word, as the Dlt QP is NULL in the first byte of the QP table for AOM. */
    pLcuBuf += iLcuQpOffset;

    uint8_t uSegId = *pLcuBuf & MASK_QP_MICRO;

    int16_t deltaQP = pDeltaQpSegments[uSegId];

    return deltaQP;
}

/****************************************************************************/
template<typename T>
inline void setLCUQuality(T* pLCUQP, uint16_t uROIQP)
{
    if(uROIQP & MASK_FORCE_INTRA)
        *pLCUQP = (*pLCUQP & MASK_QP) | MASK_FORCE_INTRA;
    else if((*pLCUQP & MASK_FORCE_INTRA) && !(uROIQP & MASK_FORCE_MV0))
    {
        *pLCUQP = uROIQP | MASK_FORCE_INTRA;
    }
    else
    {
        *pLCUQP = uROIQP;
    }
}

} // anonymous namespace

/****************************************************************************/
RoiManager::RoiManager(int32_t picWidth, int32_t picHeight, AL_EProfile profile,
                       uint8_t log2MaxCuSize, int bkgQualityCode, RoiOrder order)
{
    iMinQP = AL_IS_AVC(profile) || AL_IS_HEVC(profile) ? -32 : -128;
    iMaxQP = AL_IS_AVC(profile) || AL_IS_HEVC(profile) ? 31 : 127;
    bIsAOM = AL_IS_AOM(profile);
    iPicWidth = picWidth;
    iPicHeight = picHeight;
    uLog2MaxCuSize = log2MaxCuSize;
    eDefaultBkgQuality = bkgQualityCode;
    eDefaultOrder = order;
    nextId_ = 0;
    currentFrame_ = 0;
    pDeltaQpSegments = nullptr;

    iLcuPicWidth = AL_RoundUp(iPicWidth, 1 << uLog2MaxCuSize) >> uLog2MaxCuSize;
    iLcuPicHeight = AL_RoundUp(iPicHeight, 1 << uLog2MaxCuSize) >> uLog2MaxCuSize;
    iNumLCUs = iLcuPicWidth * iLcuPicHeight;
}

/****************************************************************************/
RoiManager::~RoiManager()
{
}

/****************************************************************************/
RoiManager::RegionDef* RoiManager::find(int32_t id)
{
    for(auto& r : regions_)
        if(r.id == id)
            return &r;
    return nullptr;
}

/****************************************************************************/
bool RoiManager::activeAt(const RegionDef& region, int32_t frameIdx) const
{
    return frameIdx >= region.enableFrame && frameIdx < region.disableFrame;
}

/****************************************************************************/
RoiManager::Node RoiManager::toNode(const RegionDef& region) const
{
    int32_t posX = region.posX >> uLog2MaxCuSize;
    int32_t posY = region.posY >> uLog2MaxCuSize;
    int32_t width = AL_RoundUp(region.width, 1 << uLog2MaxCuSize) >> uLog2MaxCuSize;
    int32_t height = AL_RoundUp(region.height, 1 << uLog2MaxCuSize) >> uLog2MaxCuSize;

    Node node;
    node.iPosX = posX;
    node.iPosY = posY;
    node.iWidth = ((posX + width) > iLcuPicWidth) ? (iLcuPicWidth - posX) : width;
    node.iHeight = ((posY + height) > iLcuPicHeight) ? (iLcuPicHeight - posY) : height;
    node.iDeltaQP = getNewDeltaQP(region.qualityCode);
    return node;
}

/****************************************************************************/
int32_t RoiManager::addRegion(int32_t posX, int32_t posY, int32_t width, int32_t height,
                              int qualityCode, bool background)
{
    std::lock_guard<std::mutex> lock(mtx_);
    RegionDef region;
    region.id = nextId_++;
    region.posX = posX;
    region.posY = posY;
    region.width = width;
    region.height = height;
    region.qualityCode = qualityCode;
    region.background = background;
    region.enableFrame = INT32_MAX;   // starts disabled
    region.disableFrame = INT32_MAX;
    regions_.push_back(region);
    return region.id;
}

/****************************************************************************/
void RoiManager::enableRegion(int32_t id, int32_t frameIdx)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(RegionDef* r = find(id))
    {
        r->enableFrame = frameIdx;
        r->disableFrame = INT32_MAX;
    }
}

/****************************************************************************/
void RoiManager::disableRegion(int32_t id, int32_t frameIdx)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(RegionDef* r = find(id))
        r->disableFrame = frameIdx;
}

/****************************************************************************/
void RoiManager::setOrder(int32_t frameIdx, RoiOrder order)
{
    std::lock_guard<std::mutex> lock(mtx_);
    orderChanges_.emplace_back(frameIdx, order);
}

/****************************************************************************/
bool RoiManager::isActive(int32_t id) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    for(auto const& r : regions_)
        if(r.id == id)
            return currentFrame_ >= r.enableFrame && currentFrame_ < r.disableFrame;
    return false;
}

/****************************************************************************/
void RoiManager::meanQuality(uint8_t* pTargetQP, uint8_t* iDQp1, uint8_t iDQp2,
                             int32_t iNumQPPerLCU, int32_t iLcuQpOffset)
{
    auto eMask = (*pTargetQP & MASK_FORCE);

    uint8_t uDeltaQP = translateSegIDToDltQP(pDeltaQpSegments, iDQp1, bIsAOM, iLcuQpOffset);
    int8_t iQP = clip3((getDQp(uDeltaQP) + getDQp(iDQp2)) / 2, iMinQP, iMaxQP) & MASK_QP;

    if(bIsAOM)
        iQP = getSegmentId(pDeltaQpSegments, iQP);

    if(iLcuQpOffset && !bIsAOM)
        pTargetQP[0] = iQP | eMask;
    else
        for(int32_t i = iLcuQpOffset; i < iNumQPPerLCU; ++i)
            pTargetQP[i] = iQP | eMask;
}

/****************************************************************************/
void RoiManager::updateTransitionHorz(uint8_t* pLcu1, uint8_t* pLcu2, int32_t iNumQPPerLCU,
        int32_t iNumBytesPerLCU, int32_t iLcuPicWidth, int32_t iPosX, int32_t iWidth,
        int8_t iQP, int32_t iLcuQpOffset)
{
    /* Left corner */
    if(iPosX > 1)
        meanQuality(&pLcu1[-iNumBytesPerLCU], &pLcu2[-2 * iNumBytesPerLCU], iQP, iNumQPPerLCU,
                    iLcuQpOffset);
    else if(iPosX > 0)
        meanQuality(&pLcu1[-iNumBytesPerLCU], &pLcu2[-iNumBytesPerLCU], iQP, iNumQPPerLCU,
                    iLcuQpOffset);

    /* Width */
    for(int32_t w = 0; w < iWidth; ++w)
        meanQuality(&pLcu1[w * iNumBytesPerLCU], &pLcu2[w * iNumBytesPerLCU], iQP, iNumQPPerLCU,
                    iLcuQpOffset);

    /* Right corner */
    if(iPosX + iWidth + 1 < iLcuPicWidth)
        meanQuality(&pLcu1[iWidth * iNumBytesPerLCU], &pLcu2[(iWidth + 1) * iNumBytesPerLCU], iQP,
                    iNumQPPerLCU, iLcuQpOffset);
    else if(iPosX + iWidth < iLcuPicWidth)
        meanQuality(&pLcu1[iWidth * iNumBytesPerLCU], &pLcu2[iWidth * iNumBytesPerLCU], iQP,
                     iNumQPPerLCU, iLcuQpOffset);
}

/****************************************************************************/
void RoiManager::updateTransitionVert(uint8_t* pLcu1, uint8_t* pLcu2, int32_t iNumQPPerLCU,
        int32_t iNumBytesPerLCU, int32_t iLcuPicWidth, int32_t iHeight, int8_t iQP,
        int32_t iLcuQpOffset)
{
    for(int32_t h = 0; h < iHeight; ++h)
    {
        meanQuality(pLcu1, pLcu2, iQP, iNumQPPerLCU, iLcuQpOffset);
        pLcu1 += (iLcuPicWidth * iNumBytesPerLCU);
        pLcu2 += (iLcuPicWidth * iNumBytesPerLCU);
    }
}

/****************************************************************************/
uint32_t RoiManager::getNodePosInBuf(uint32_t uLcuX, uint32_t uLcuY, int32_t iNumBytesPerLCU) const
{
    uint32_t uLcuNum = uLcuY * iLcuPicWidth + uLcuX;
    return uLcuNum * iNumBytesPerLCU;
}

/****************************************************************************/
void RoiManager::computeROI(int32_t iNumQPPerLCU, int32_t iNumBytesPerLCU, uint8_t* pQPs,
                            int32_t iLcuQpOffset, const Node& region)
{
    auto* pLCU = pQPs + getNodePosInBuf(region.iPosX, region.iPosY, iNumBytesPerLCU);
    uint16_t uDeltaQPOrSegId = region.iDeltaQP;

    if(bIsAOM && !(region.iDeltaQP & MASK_FORCE))
        uDeltaQPOrSegId = getSegmentId(pDeltaQpSegments, region.iDeltaQP);

    /* Fill ROI */
    for(int32_t h = 0; h < region.iHeight; ++h)
    {
        for(int32_t w = 0; w < region.iWidth; ++w)
        {
            if(iLcuQpOffset)
            {
                uint16_t uQPOrSegIdAndFlags = region.iDeltaQP;

                if(bIsAOM)
                    uQPOrSegIdAndFlags = region.iDeltaQP & MASK_FORCE;
                setLCUQuality<uint16_t>((uint16_t*)(pLCU + w * iNumBytesPerLCU), uQPOrSegIdAndFlags);
            }

            if(!iLcuQpOffset || bIsAOM)
            {
                for(int32_t i = 0; i < iNumQPPerLCU - iLcuQpOffset; ++i)
                {
                    uDeltaQPOrSegId = ((MASK_FORCE & region.iDeltaQP) >> (MASK_QP_NUMBITS - 6))
                                      | uDeltaQPOrSegId;
                    setLCUQuality<uint8_t>(pLCU + w * iNumBytesPerLCU + iLcuQpOffset + i,
                                           uDeltaQPOrSegId);
                }
            }
        }

        pLCU += iNumBytesPerLCU * iLcuPicWidth;
    }

    if(!(region.iDeltaQP & MASK_FORCE))
    {
        /* Update above transition. */
        if(region.iPosY)
        {
            uint8_t* pLcuTop1 = pQPs + getNodePosInBuf(region.iPosX, region.iPosY - 1, iNumBytesPerLCU);
            uint8_t* pLcuTop2 = pLcuTop1;

            if(region.iPosY > 1)
                pLcuTop2 = pQPs + getNodePosInBuf(region.iPosX, region.iPosY - 2, iNumBytesPerLCU);

            updateTransitionHorz(pLcuTop1, pLcuTop2, iNumQPPerLCU, iNumBytesPerLCU, iLcuPicWidth,
                    region.iPosX, region.iWidth, region.iDeltaQP, iLcuQpOffset);
        }

        /* Update below transition. */
        if(region.iPosY + region.iHeight < iLcuPicHeight)
        {
            uint8_t* pLcuBot1 = pQPs + getNodePosInBuf(region.iPosX, region.iPosY + region.iHeight,
                                                       iNumBytesPerLCU);
            uint8_t* pLcuBot2 = pLcuBot1;

            if(region.iPosY + region.iHeight + 2 < iLcuPicHeight)
                pLcuBot2 = pQPs + getNodePosInBuf(region.iPosX, region.iPosY + region.iHeight + 1,
                                                  iNumBytesPerLCU);

            updateTransitionHorz(pLcuBot1, pLcuBot2, iNumQPPerLCU, iNumBytesPerLCU, iLcuPicWidth,
                    region.iPosX, region.iWidth, region.iDeltaQP, iLcuQpOffset);
        }

        /* Update left transition. */
        if(region.iPosX)
        {
            uint8_t* pLcuLeft1 = pQPs + getNodePosInBuf(region.iPosX - 1, region.iPosY,
                                                        iNumBytesPerLCU);
            uint8_t* pLcuLeft2 = pLcuLeft1;

            if(region.iPosX > 1)
                pLcuLeft2 = pQPs + getNodePosInBuf(region.iPosX - 2, region.iPosY, iNumBytesPerLCU);

            updateTransitionVert(pLcuLeft1, pLcuLeft2, iNumQPPerLCU, iNumBytesPerLCU, iLcuPicWidth,
                                 region.iHeight, region.iDeltaQP, iLcuQpOffset);
        }

        /* Update right transition. */
        if(region.iPosX + region.iWidth < iLcuPicWidth)
        {
            uint8_t* pLcuRight1 = pQPs + getNodePosInBuf(region.iPosX + region.iWidth, region.iPosY,
                                                         iNumBytesPerLCU);
            uint8_t* pLcuRight2 = pLcuRight1;

            if(region.iPosX + region.iWidth + 2 < iLcuPicWidth)
                pLcuRight2 = pQPs + getNodePosInBuf(region.iPosX + region.iWidth + 1, region.iPosY,
                                                    iNumBytesPerLCU);

            updateTransitionVert(pLcuRight1, pLcuRight2, iNumQPPerLCU, iNumBytesPerLCU, iLcuPicWidth,
                                 region.iHeight, region.iDeltaQP, iLcuQpOffset);
        }
    }
}

/****************************************************************************/
void RoiManager::fillBuffer(int32_t frameIdx, int32_t iNumQPPerLCU, int32_t iNumBytesPerLCU,
                            uint8_t* pQPs, int32_t iLcuQpOffset)
{
    if(pQPs == nullptr)
        throw std::runtime_error("pQPs buffer must exist");

    std::lock_guard<std::mutex> lock(mtx_);
    currentFrame_ = frameIdx;

    // Resolve the overlap order scheduled for this frame (latest change with frame <= frameIdx).
    RoiOrder order = eDefaultOrder;
    int32_t bestOrderFrame = -1;
    for(auto const& oc : orderChanges_)
        if(oc.first <= frameIdx && oc.first >= bestOrderFrame)
        {
            bestOrderFrame = oc.first;
            order = oc.second;
        }

    // Resolve the background: the most-recently-enabled active background wins, else default.
    int bkgCode = eDefaultBkgQuality;
    int32_t bestBkgFrame = -1;
    for(auto const& r : regions_)
        if(r.background && activeAt(r, frameIdx) && r.enableFrame >= bestBkgFrame)
        {
            bestBkgFrame = r.enableFrame;
            bkgCode = r.qualityCode;
        }

    // Build the ordered list of active foreground regions (snapped to the LCU grid).
    std::list<Node> nodes;
    for(auto const& r : regions_)
    {
        if(r.background || !activeAt(r, frameIdx))
            continue;
        if(r.posX >= iPicWidth || r.posY >= iPicHeight)
            continue;

        Node node = toNode(r);
        if(order == RoiOrder::QUALITY)
        {
            // Keep higher-quality (lower-QP) regions ahead so they win on overlap.
            auto it = std::find_if(nodes.begin(), nodes.end(),
                    [&](const Node& c) { return !shouldInsertAfter(c.iDeltaQP, node.iDeltaQP); });
            nodes.insert(it, node);
        }
        else
            nodes.push_back(node);
    }

    uint16_t uDeltaQP = getNewDeltaQP(bkgCode);
    uint16_t uBkgQPOrSegId = uDeltaQP;

    if(bIsAOM)
    {
        pDeltaQpSegments = (int16_t*)(pQPs - EP2_BUF_SEG_CTRL.Size);
        pDeltaQpSegments[0] = Q_HIGH;
        pDeltaQpSegments[1] = -3;
        pDeltaQpSegments[2] = Q_MEDIUM;
        pDeltaQpSegments[3] = 3;
        pDeltaQpSegments[4] = Q_LOW;
        pDeltaQpSegments[5] = 10;
        pDeltaQpSegments[6] = 15;
        pDeltaQpSegments[7] = Q_DONT_CARE;

        uBkgQPOrSegId = getSegmentId(pDeltaQpSegments, uDeltaQP);
    }

    /* Fill background */
    for(int32_t iLCU = 0; iLCU < iNumLCUs; iLCU++)
    {
        int32_t iFirst = iLCU * iNumBytesPerLCU;

        /* iLcuQpOffset distinguishes QP Table V1 (0) from V2 (4). */
        if(iLcuQpOffset)
        {
            /* For HEVC & AVC, fill only the info of the macro-block, reused for all sub-blocks. */
            if(!bIsAOM)
                pQPs[iFirst] = uBkgQPOrSegId & MASK_QP;
            pQPs[iFirst + 1] = (uDeltaQP & MASK_FORCE) >> 8;
            pQPs[iFirst + 3] = DEFAULT_LAMBDA_FACT;
        }

        if(!iLcuQpOffset || bIsAOM)
        {
            for(int32_t iQP = iLcuQpOffset; iQP < iNumQPPerLCU; ++iQP)
            {
                pQPs[iFirst + iQP] = ((MASK_FORCE & uDeltaQP) >> (MASK_QP_NUMBITS - 6))
                                     | uBkgQPOrSegId;
            }
        }
    }

    /* Fill ROIs */
    for(const Node& node : nodes)
        computeROI(iNumQPPerLCU, iNumBytesPerLCU, pQPs, iLcuQpOffset, node);

    if(bIsAOM)
        for(int32_t i = 0; i < AL_QPTABLE_NUM_SEGMENTS; ++i)
            pDeltaQpSegments[i] *= 5;
}

} // namespace vcucodec
} // namespace cv
