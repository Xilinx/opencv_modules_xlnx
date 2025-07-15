// Modifications Copyright(C) [2025] Advanced Micro Devices, Inc. All rights reserved)

// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <fstream>
#include "CodecUtils.h"
#include "lib_app/utils.h"

extern "C"
{
#include "lib_encode/lib_encoder.h"
}

struct GMV final
{
  ~GMV(void) = default;

  explicit GMV(std::string const& sGMVFileName, int32_t iFirstFrame = 0) : m_iFirstFrame(iFirstFrame)
  {
    if(sGMVFileName.empty())
      return;

    OpenInput(m_input, sGMVFileName, false);

    while(iFirstFrame--)
      ReadNextFrameMV(m_input, m_iNextGMV_x, m_iNextGMV_y);

    m_iNextFrame = ReadNextFrameMV(m_input, m_iNextGMV_x, m_iNextGMV_y);
  }

  void notify(AL_HEncoder hEnc)
  {
    if(!m_input.is_open())
      return;

    if(m_iNextFrame != -1)
      AL_Encoder_NotifyGMV(hEnc, m_iNextFrame - m_iFirstFrame, m_iNextGMV_x, m_iNextGMV_y);
    m_iNextFrame = ReadNextFrameMV(m_input, m_iNextGMV_x, m_iNextGMV_y);
  }

private:
  std::ifstream m_input;
  int32_t const m_iFirstFrame;
  int32_t m_iNextFrame;
  int32_t m_iNextGMV_x;
  int32_t m_iNextGMV_y;
};
