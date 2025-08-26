// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#include "CommandsSender.h"

using namespace std;

void CommandsSender::notifySceneChange(int32_t lookAhead)
{
  AL_Encoder_NotifySceneChange(hEnc, lookAhead);
}

void CommandsSender::notifyIsLongTerm(void)
{
  AL_Encoder_NotifyIsLongTerm(hEnc);
}

void CommandsSender::notifyUseLongTerm(void)
{
  AL_Encoder_NotifyUseLongTerm(hEnc);
}

#include <iostream>

#define CHECK(statement) \
  if(!statement) \
    std::cerr << # statement << " failed with error : " << AL_Encoder_GetLastError(hEnc) << std::endl

void CommandsSender::restartGop(void)
{
  CHECK(AL_Encoder_RestartGop(hEnc));
}

void CommandsSender::restartGopRecoveryPoint(void)
{
  CHECK(AL_Encoder_RestartGopRecoveryPoint(hEnc));
}

void CommandsSender::setGopLength(int32_t gopLength)
{
  CHECK(AL_Encoder_SetGopLength(hEnc, gopLength));
}

void CommandsSender::setNumB(int32_t numB)
{
  CHECK(AL_Encoder_SetGopNumB(hEnc, numB));
}

void CommandsSender::setFreqIDR(int32_t freqIDR)
{
  CHECK(AL_Encoder_SetFreqIDR(hEnc, freqIDR));
}

void CommandsSender::setFrameRate(int32_t frameRate, int32_t clockRatio)
{
  CHECK(AL_Encoder_SetFrameRate(hEnc, frameRate, clockRatio));
}

void CommandsSender::setBitRate(int32_t bitRate)
{
  CHECK(AL_Encoder_SetBitRate(hEnc, bitRate));
}

void CommandsSender::setMaxBitRate(int32_t iTargetBitRate, int32_t iMaxBitRate)
{
  CHECK(AL_Encoder_SetMaxBitRate(hEnc, iTargetBitRate, iMaxBitRate));
}

void CommandsSender::setQP(int32_t qp)
{
  CHECK(AL_Encoder_SetQP(hEnc, qp));
}

void CommandsSender::setQPOffset(int32_t iQpOffset)
{
  CHECK(AL_Encoder_SetQPOffset(hEnc, iQpOffset));
}

void CommandsSender::setQPBounds(int32_t iMinQP, int32_t iMaxQP)
{
  CHECK(AL_Encoder_SetQPBounds(hEnc, iMinQP, iMaxQP));
}

void CommandsSender::setQPBounds_I(int32_t iMinQP_I, int32_t iMaxQP_I)
{
  CHECK(AL_Encoder_SetQPBoundsPerFrameType(hEnc, iMinQP_I, iMaxQP_I, AL_SLICE_I));
}

void CommandsSender::setQPBounds_P(int32_t iMinQP_P, int32_t iMaxQP_P)
{
  CHECK(AL_Encoder_SetQPBoundsPerFrameType(hEnc, iMinQP_P, iMaxQP_P, AL_SLICE_P));
}

void CommandsSender::setQPBounds_B(int32_t iMinQP_B, int32_t iMaxQP_B)
{
  CHECK(AL_Encoder_SetQPBoundsPerFrameType(hEnc, iMinQP_B, iMaxQP_B, AL_SLICE_B));
}

void CommandsSender::setQPIPDelta(int32_t iQPDelta)
{
  CHECK(AL_Encoder_SetQPIPDelta(hEnc, iQPDelta));
}

void CommandsSender::setQPPBDelta(int32_t iQPDelta)
{
  CHECK(AL_Encoder_SetQPPBDelta(hEnc, iQPDelta));
}

void CommandsSender::setDynamicInput(int32_t iInputIdx)
{
  bInputChanged = true;
  this->iInputIdx = iInputIdx;
}

void CommandsSender::setLFMode(int32_t iMode)
{
  CHECK(AL_Encoder_SetLoopFilterMode(hEnc, iMode));
}

void CommandsSender::setLFBetaOffset(int32_t iBetaOffset)
{
  CHECK(AL_Encoder_SetLoopFilterBetaOffset(hEnc, iBetaOffset));
}

void CommandsSender::setLFTcOffset(int32_t iTcOffset)
{
  CHECK(AL_Encoder_SetLoopFilterTcOffset(hEnc, iTcOffset));
}

void CommandsSender::setCostMode(bool bCostMode)
{
  CHECK(AL_Encoder_SetCostMode(hEnc, bCostMode));
}

void CommandsSender::setMaxPictureSize(int32_t iMaxPictureSize)
{
  CHECK(AL_Encoder_SetMaxPictureSize(hEnc, iMaxPictureSize));
}

void CommandsSender::setMaxPictureSize_I(int32_t iMaxPictureSize)
{
  CHECK(AL_Encoder_SetMaxPictureSizePerFrameType(hEnc, iMaxPictureSize, AL_SLICE_I));
}

void CommandsSender::setMaxPictureSize_P(int32_t iMaxPictureSize)
{
  CHECK(AL_Encoder_SetMaxPictureSizePerFrameType(hEnc, iMaxPictureSize, AL_SLICE_P));
}

void CommandsSender::setMaxPictureSize_B(int32_t iMaxPictureSize)
{
  CHECK(AL_Encoder_SetMaxPictureSizePerFrameType(hEnc, iMaxPictureSize, AL_SLICE_B));
}

void CommandsSender::setQPChromaOffsets(int32_t iQp1Offset, int32_t iQp2Offset)
{
  CHECK(AL_Encoder_SetQPChromaOffsets(hEnc, iQp1Offset, iQp2Offset));
}

void CommandsSender::setAutoQP(bool bUseAutoQP)
{
  CHECK(AL_Encoder_SetAutoQP(hEnc, bUseAutoQP));
}

void CommandsSender::setHDRIndex(int32_t iHDRIdx)
{
  bHDRChanged = true;
  this->iHDRIdx = iHDRIdx;
}

void CommandsSender::Reset(void)
{
  bInputChanged = false;
  bHDRChanged = false;
}

bool CommandsSender::HasInputChanged(int& iInputIdx)
{
  iInputIdx = this->iInputIdx;
  return bInputChanged;
}

bool CommandsSender::HasHDRChanged(int& iHDRIdx)
{
  iHDRIdx = this->iHDRIdx;
  return bHDRChanged;
}
