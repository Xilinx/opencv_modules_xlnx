// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <fstream>
#include <string>
#include <stdexcept>
#include <deque>

#include "config.h" //CJQ

extern "C"
{
#include "lib_common/Error.h"
#include "lib_common/SliceConsts.h"
#include "lib_common/BufferAPI.h"
#include "lib_common_enc/Settings.h"
}

/*****************************************************************************/
uint32_t ReadNextFrame(std::ifstream& File);

/*****************************************************************************/
uint32_t ReadNextFrameMV(std::ifstream& File, int& iX, int& iY);

/*****************************************************************************/
void DisplayFrameStatus(int32_t iFrameNum);

/*****************************************************************************/

class codec_error : public std::runtime_error
{
public:
  explicit codec_error(const std::string& _Message, AL_ERR eErrCode) : std::runtime_error(_Message), m_eErrCode(eErrCode)
  { // construct from message string
  }

  explicit codec_error(const char* _Message, AL_ERR eErrCode) : std::runtime_error(_Message), m_eErrCode(eErrCode)
  { // construct from message string
  }

  AL_ERR GetCode() const { return m_eErrCode; }

protected:
  AL_ERR m_eErrCode;
};

/*****************************************************************************/
int32_t WriteStream(std::ofstream& File, AL_TBuffer* pStream, const AL_TEncSettings* pSettings, std::streampos& iHdrPos, int& iFrameSize);

/*****************************************************************************/
struct ImageSize
{
  int32_t size; // in bytes
  bool finished;
};

void GetImageStreamSize(AL_TBuffer* pStream, std::deque<ImageSize>& imageSizes);
