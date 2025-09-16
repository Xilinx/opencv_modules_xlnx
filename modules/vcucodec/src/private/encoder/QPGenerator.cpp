// Modifications Copyright(C) [2025] Advanced Micro Devices, Inc. All rights reserved)

// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

/****************************************************************************
   -----------------------------------------------------------------------------
****************************************************************************/
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <stdexcept>

extern "C"
{
#include "lib_rtos/lib_rtos.h"
#include "lib_common/BufferAPI.h"
#include "lib_common/Error.h"
#include "lib_common/Round.h"
#include "lib_common_enc/EncBuffers.h"
#include "lib_common_enc/QpTableMeta.h"
}

#include "QPGenerator.h"
#include "ROIMngr.h"
#include "config.h" //CJQ

#include "lib_app/FileUtils.hpp"

using namespace std;

/****************************************************************************/
static bool GetTag(ifstream& qpFile, AL_ESliceType& eType)
{
  eType = AL_SLICE_MAX_ENUM;
  streampos curPos;
  string sTag;

  do
  {
    curPos = qpFile.tellg();

    string sLine;
    getline(qpFile, sLine);
    auto beg = sLine.find_first_not_of(" \t");
    auto end = sLine.find_last_not_of(" \t");

    if(beg == sLine.npos)
      beg = 0;

    if(end != sLine.npos)
      end += 1;

    sTag = sLine.substr(beg, end);
  }
  while(!qpFile.eof() && (sTag.empty() || sTag[0] == '#' || (sTag[0] == '/' && sTag[1] == '/')));

  if(sTag == "[I]")
    eType = AL_SLICE_I;
  else if(sTag == "[P]")
    eType = AL_SLICE_P;
  else if(sTag == "[B]")
    eType = AL_SLICE_B;
  else
    qpFile.seekg(curPos);

  return eType != AL_SLICE_MAX_ENUM;
}

/****************************************************************************/
static AL_TQpTableMetaData* GetQpTableMetaData(AL_TBuffer* pQpBuf)
{
  AL_TQpTableMetaData* pMeta = (AL_TQpTableMetaData*)AL_Buffer_GetMetaData(pQpBuf, AL_META_QP_TABLE);

  if(pMeta)
    return pMeta;

  pMeta = nullptr; //AL_QpTableMetaData_Create();
  AL_Buffer_AddMetaData(pQpBuf, (AL_TMetaData*)pMeta);

  pMeta->tQpTable[AL_SLICE_P].iChunkIdx = pMeta->tQpTable[AL_SLICE_I].iChunkIdx = 0;
  pMeta->tQpTable[AL_SLICE_P].uOffset = pMeta->tQpTable[AL_SLICE_I].uOffset = 0;

  int32_t iChunkID = AL_Buffer_AllocateChunkNamed(pQpBuf, AL_Buffer_GetSizeChunk(pQpBuf, 0), "qp-ext");

  if(iChunkID != AL_BUFFER_BAD_CHUNK)
  {
    Rtos_Memset(AL_Buffer_GetDataChunk(pQpBuf, iChunkID), 0, AL_Buffer_GetSizeChunk(pQpBuf, iChunkID));
    pMeta->tQpTable[AL_SLICE_P].iChunkIdx = iChunkID;
    pMeta->tQpTable[AL_SLICE_P].uOffset = 0;
  }

  pMeta->tQpTable[AL_SLICE_B] = pMeta->tQpTable[AL_SLICE_P];

  iChunkID = AL_Buffer_AllocateChunkNamed(pQpBuf, AL_Buffer_GetSizeChunk(pQpBuf, 0), "qp-ext");

  if(iChunkID != AL_BUFFER_BAD_CHUNK)
  {
    Rtos_Memset(AL_Buffer_GetDataChunk(pQpBuf, iChunkID), 0, AL_Buffer_GetSizeChunk(pQpBuf, iChunkID));
    pMeta->tQpTable[AL_SLICE_B].iChunkIdx = iChunkID;
    pMeta->tQpTable[AL_SLICE_B].uOffset = 0;
  }

  return pMeta;
}

/****************************************************************************/
static AL_ERR ReadQPs(ifstream& qpFile, uint8_t* pQPs, int32_t iNumLCUs, int32_t iNumQPPerLCU, int32_t iNumBytesPerLCU, int32_t iQPTableDepth)
{
  int32_t iNumQPPerLine;
  (void)iQPTableDepth;
#ifdef HAVE_VCU2_CTRLSW
  auto const bQPTableDepth2 = iQPTableDepth == 2;

  if(bQPTableDepth2)
    iNumQPPerLine = 4;
  else
#endif
  iNumQPPerLine = (iNumQPPerLCU == 5) ? 5 : 1;
  int32_t iNumDigit = iNumQPPerLine * 2;

  int32_t iIdx = 0;

  for(int32_t iLCU = 0; iLCU < iNumLCUs; ++iLCU)
  {
    string sLine;

    int32_t iFirst = iNumBytesPerLCU * iLCU;
    int32_t iNumLine = 0;

    for(int32_t iQP = 0; iQP < iNumQPPerLCU; ++iQP)
    {
      if(iIdx == 0)
      {
        getline(qpFile, sLine);

#ifdef HAVE_VCU2_CTRLSW
        if(sLine.size() < uint32_t(iNumDigit))
          return AL_ERR_QPLOAD_NOT_ENOUGH_DATA;
#else

        if(sLine.empty())
          return AL_ERR_QPLOAD_NOT_ENOUGH_DATA;

        if(sLine.size() < uint32_t(iNumDigit))
          return AL_ERR_QPLOAD_DATA;
#endif
        ++iNumLine;
      }

      int32_t iHexVal = FromHex2(sLine[iNumDigit - 2 * iIdx - 2], sLine[iNumDigit - 2 * iIdx - 1]);

      if(iHexVal == FROM_HEX_ERROR)
        return AL_ERR_QPLOAD_DATA;

      pQPs[iFirst + iQP] = iHexVal;

      iIdx = (iIdx + 1) % iNumQPPerLine;
    }
#ifdef HAVE_VCU2_CTRLSW
    if(bQPTableDepth2)
    {
      auto const iLinePerLcu = 6;

      // alignment
      if(iNumLine == iLinePerLcu)
      {
        for(int32_t iQP = 0; iQP < 8; ++iQP)
          pQPs[iNumQPPerLCU + iQP] = 0;
      }
      else
      {
        while(iNumLine < iLinePerLcu)
        {
          getline(qpFile, sLine);
          ++iNumLine;
        }
      }
    }
#endif
  }

  return AL_SUCCESS;
}

#ifdef _MSC_VER
#include <cstdarg>
static inline
int32_t LIBSYS_SNPRINTF(char* str, size_t size, const char* format, ...)
{
  int32_t retval;
  va_list ap;
  va_start(ap, format);
  retval = _vsnprintf(str, size, format, ap);
  va_end(ap);
  return retval;
}

#define snprintf LIBSYS_SNPRINTF
#endif

static const string DefaultQPTablesFolder = ".";
static const string QPTablesMotif = "QP";
static const string QPTablesLegacyMotif = "QPs"; // Use default file (motif + "s" for backward compatibility)
static const string QPTablesExtension = ".hex";

static string createQPFileName(const string& folder, const string& motif)
{
  return CombinePath(folder, motif + QPTablesExtension);
}

/****************************************************************************/
static bool OpenFile(const string& sQPTablesFolder, int32_t iFrameID, string motif, ifstream& File)
{
  string sFileFolder = sQPTablesFolder.empty() ? DefaultQPTablesFolder : sQPTablesFolder;

  auto qpFileName = CreateFileNameWithID(sFileFolder, motif, QPTablesExtension, iFrameID);

  File.open(qpFileName);

  if(!File.is_open())
  {
    qpFileName = createQPFileName(sFileFolder, QPTablesLegacyMotif);
    File.open(qpFileName);
  }

  return File.is_open();
}

/****************************************************************************/
static AL_ERR Load_QPTable_FromFile_AOM(ifstream& file, uint8_t* pQpData, int32_t iNumLCUs, int32_t iNumQPPerLCU, int32_t iNumBytesPerLCU, int32_t iQPTableDepth, bool bRelative)
{
  string sLine;
  int16_t* pSeg = reinterpret_cast<int16_t*>(pQpData + EP2_BUF_SEG_CTRL.Offset);

  Rtos_Memset(pSeg, 0, AL_QPTABLE_SEGMENTS_SIZE);

  for(int32_t iSeg = 0; iSeg < 8; ++iSeg)
  {
    int32_t idx = (iSeg & 0x01) << 2;

    if(idx == 0)
    {
      getline(file, sLine);

      if(sLine.size() < 8)
        return AL_ERR_QPLOAD_NOT_ENOUGH_DATA;
    }

    int32_t iHexVal = FromHex4(sLine[4 - idx], sLine[5 - idx], sLine[6 - idx], sLine[7 - idx]);

    if(iHexVal == FROM_HEX_ERROR)
      return AL_ERR_QPLOAD_DATA;

    pSeg[iSeg] = iHexVal;

    int16_t iMinQP = 1;
    int16_t iMaxQP = 255;

    if(bRelative)
    {
      int16_t iMaxDelta = iMaxQP - iMinQP;
      iMinQP = -iMaxDelta;
      iMaxQP = +iMaxDelta;
    }

    pSeg[iSeg] = max(iMinQP, min(iMaxQP, pSeg[iSeg]));
  }

  uint8_t* pQPs = pQpData + EP2_BUF_QP_BY_MB.Offset;

  return ReadQPs(file, pQPs, iNumLCUs, iNumQPPerLCU, iNumBytesPerLCU, iQPTableDepth);
}

/****************************************************************************/
static AL_ERR Load_QPTable_FromFile(ifstream& file, uint8_t* pQpData, int32_t iNumLCUs, int32_t iNumQPPerLCU, int32_t iNumBytesPerLCU, int32_t iQPTableDepth)
{
  uint8_t* pQPs = pQpData + EP2_BUF_QP_BY_MB.Offset;

  // Warning: the LOAD_QP is not backward compatible
  return ReadQPs(file, pQPs, iNumLCUs, iNumQPPerLCU, iNumBytesPerLCU, iQPTableDepth);
}

/****************************************************************************/
static bool get_motif(char* sLine, string motif, int& iPos)
{
  int32_t iState = 0;
  int32_t iNumState = motif.length();
  iPos = 0;

  while(iState < iNumState && sLine[iPos] != '\0')
  {
    if(sLine[iPos] == motif[iState])
      ++iState;
    else
      iState = 0;

    ++iPos;
  }

  return iState == iNumState;
}

/****************************************************************************/
static int32_t get_id(char* sLine, int32_t iPos)
{
  return atoi(sLine + iPos);
}

/****************************************************************************/
static bool check_frame_id(char* sLine, int32_t iFrameID)
{
  int32_t iPos;

  if(!get_motif(sLine, "frame", iPos))
    return false;
  int32_t iID = get_id(sLine, iPos);

  return iID == iFrameID;
}

/****************************************************************************/
static string getStringOnKeyword(const string& sLine, int32_t iPos)
{
  int32_t iPos1 = sLine.find_first_not_of("\t= ", iPos);
  int32_t iPos2 = sLine.find_first_of(", \n\r\0\t", iPos1);
  return sLine.substr(iPos1, iPos2 - iPos1);
}

/****************************************************************************/
static AL_ERoiQuality get_roi_quality(char* sLine, int32_t iPos)
{
  auto s = getStringOnKeyword(sLine, iPos);

  if(s == "HIGH_QUALITY")
    return AL_ROI_QUALITY_HIGH;

  if(s == "MEDIUM_QUALITY")
    return AL_ROI_QUALITY_MEDIUM;

  if(s == "LOW_QUALITY")
    return AL_ROI_QUALITY_LOW;

  if(s == "NO_QUALITY")
    return AL_ROI_QUALITY_DONT_CARE;
#ifdef HAVE_VCU2_CTRLSW
  if(s == "STATIC_QUALITY")
    return AL_ROI_QUALITY_STATIC;
#endif
  if(s == "INTRA_QUALITY")
    return AL_ROI_QUALITY_INTRA;

  std::stringstream ss {};
  ss.str(s);
  int32_t i;
  ss >> i;
  return static_cast<AL_ERoiQuality>(i);
}

/****************************************************************************/
static AL_ERoiOrder get_roi_order(char* sLine, int32_t iPos)
{
  auto s = getStringOnKeyword(sLine, iPos);

  if(s.compare("INCOMING_ORDER") == 0)
    return AL_ROI_INCOMING_ORDER;
  return AL_ROI_QUALITY_ORDER;
}

/****************************************************************************/
static bool ReadRoiHdr(ifstream& RoiFile, int32_t iFrameID, AL_ERoiQuality& eBkgQuality, AL_ERoiOrder& eRoiOrder, bool& bRoiDisable)
{
  char sLine[256];
  bool bFind = false;

  while(!bFind && !RoiFile.eof())
  {
    RoiFile.getline(sLine, 256);
    bFind = check_frame_id(sLine, iFrameID);

    if(bFind)
    {
      int32_t iPos;

      if(get_motif(sLine, "BkgQuality", iPos))
        eBkgQuality = get_roi_quality(sLine, iPos);

      if(get_motif(sLine, "Order", iPos))
        eRoiOrder = get_roi_order(sLine, iPos);

      bRoiDisable = get_motif(sLine, "RoiDisable", iPos);
    }
  }

  return bFind;
}

/****************************************************************************/
static bool line_is_empty(char* sLine)
{
  int32_t iPos = 0;

  while(sLine[iPos] != '\0')
  {
    if((sLine[iPos] >= 'a' && sLine[iPos] <= 'z') || (sLine[iPos] >= 'A' && sLine[iPos] <= 'Z') || (sLine[iPos] >= '0' && sLine[iPos] <= '9'))
      return false;
    ++iPos;
  }

  return true;
}

/****************************************************************************/
static void get_dual_value(char* sLine, char separator, int& iPos, int& iValue1, int& iValue2)
{
  iValue1 = atoi(sLine + iPos);

  while(sLine[++iPos] != separator)
    ;

  ++iPos;

  iValue2 = atoi(sLine + iPos);

  while(sLine[++iPos] != ',')
    ;

  ++iPos;
}

/****************************************************************************/
static bool get_new_roi(ifstream& RoiFile, int& iPosX, int& iPosY, int& iWidth, int& iHeight, AL_ERoiQuality& eQuality)
{
  char sLine[256];
  bool bFind = false;

  while(!RoiFile.eof())
  {
    RoiFile.getline(sLine, 256);
    int32_t iPos;

    if(get_motif(sLine, "frame", iPos))
      return bFind;

    if(!line_is_empty(sLine))
    {
      iPos = 0;
      get_dual_value(sLine, ':', iPos, iPosX, iPosY);
      get_dual_value(sLine, 'x', iPos, iWidth, iHeight);
      eQuality = get_roi_quality(sLine, iPos);
      return true;
    }
  }

  return false;
}

/****************************************************************************/
static int32_t GetLcuQpOffset(int32_t iQPTableDepth)
{
  int32_t iLcuQpOffset = 0;
  (void)iQPTableDepth;

  auto const bQPTableDepth2 = iQPTableDepth == 2;

  if(bQPTableDepth2)
    iLcuQpOffset = 4;

  return iLcuQpOffset;
}

/****************************************************************************/
static AL_ERR Load_QPTable_FromRoiFile(AL_TRoiMngrCtx* pCtx, string const& sRoiFileName, uint8_t* pQPs, int32_t iFrameID, int32_t iNumQPPerLCU, int32_t iNumBytesPerLCU, int32_t iQPTableDepth)
{
  ifstream file(sRoiFileName);

  if(!file.is_open())
    return AL_ERR_CANNOT_OPEN_FILE;

  bool bRoiDisable = false;
  bool bHasData = ReadRoiHdr(file, iFrameID, pCtx->eBkgQuality, pCtx->eOrder, bRoiDisable);

  if(bRoiDisable)
    return AL_ERR_ROI_DISABLE;

  if(bHasData)
  {
    AL_RoiMngr_Clear(pCtx);
    int32_t iPosX = 0, iPosY = 0, iWidth = 0, iHeight = 0;
    AL_ERoiQuality eQuality;

    while(get_new_roi(file, iPosX, iPosY, iWidth, iHeight, eQuality))
      AL_RoiMngr_AddROI(pCtx, iPosX, iPosY, iWidth, iHeight, eQuality);
  }

  auto const iLcuQpOffset = GetLcuQpOffset(iQPTableDepth);
  AL_RoiMngr_FillBuff(pCtx, iNumQPPerLCU, iNumBytesPerLCU, pQPs, iLcuQpOffset);
  return AL_SUCCESS;
}

/****************************************************************************/
static void Set_Block_Feature(uint8_t* pQPs, int32_t iNumLCUs, int32_t iNumBytesPerLCU, int32_t iQPTableDepth)
{
  uint8_t const uLambdaFactor = DEFAULT_LAMBDA_FACT; // fixed point with 5 decimal bits

  if(iQPTableDepth > 1)
    for(int32_t iLcuIdx = 0; iLcuIdx < iNumLCUs * iNumBytesPerLCU; iLcuIdx += iNumBytesPerLCU)
      pQPs[iLcuIdx + 3] = uLambdaFactor;
}

/****************************************************************************/
static void GetQPBufferParameters(int16_t iLCUPicWidth, int16_t iLCUPicHeight, AL_EProfile eProf, uint8_t uLog2MaxCuSize, int32_t iQPTableDepth, int& iNumQPPerLCU, int& iNumBytesPerLCU, int32_t& iNumLCUs, uint8_t* pQPs)
{
  (void)eProf;
  (void)uLog2MaxCuSize;
  (void)iQPTableDepth;

  if(pQPs == nullptr)
    throw runtime_error("pQPs buffer must exist");

  iNumQPPerLCU = 1;
  iNumBytesPerLCU = 1;

#ifdef HAVE_VCU2_CTRLSW
  if(iQPTableDepth == 2)
  {
    int32_t const numBlk[] = { 4, 8, 24 };
    int32_t const numBytes[] = { 4, 8, 32 };
    auto const uLog2MinBlkSize = 4; // 16x16
    auto const depth = uLog2MaxCuSize - uLog2MinBlkSize;
    iNumQPPerLCU = numBlk[depth];
    iNumBytesPerLCU = numBytes[depth];
  }
#endif
  iNumLCUs = iLCUPicWidth * iLCUPicHeight;
  int32_t const iSize = AL_RoundUp(iNumLCUs * iNumBytesPerLCU, 128);

  Rtos_Memset(pQPs, 0, iSize);
}

/****************************************************************************/
AL_ERR GenerateROIBuffer(AL_TRoiMngrCtx* pRoiCtx, string const& sRoiFileName, int16_t iLCUPicWidth, int16_t iLCUPicHeight, AL_EProfile eProf, uint8_t uLog2MaxCuSize, int32_t iQPTableDepth, int32_t iFrameID, uint8_t* pQPs)
{
  int32_t iNumQPPerLCU, iNumBytesPerLCU, iNumLCUs;
  GetQPBufferParameters(iLCUPicWidth, iLCUPicHeight, eProf, uLog2MaxCuSize, iQPTableDepth, iNumQPPerLCU, iNumBytesPerLCU, iNumLCUs, pQPs);
  return Load_QPTable_FromRoiFile(pRoiCtx, sRoiFileName, pQPs, iFrameID, iNumQPPerLCU, iNumBytesPerLCU, iQPTableDepth);
}

/****************************************************************************/
AL_ERR GenerateQPBuffer(AL_EGenerateQpMode eMode, int16_t iSliceQP, int16_t iMinQP, int16_t iMaxQP, int16_t iLCUPicWidth, int16_t iLCUPicHeight, AL_EProfile eProf, uint8_t uLog2MaxCuSize, int32_t iQPTableDepth, const string& sQPTablesFolder, int32_t iFrameID, AL_TBuffer* pQpBuf)
{
  (void)iSliceQP;
  (void)iMinQP;
  (void)iMaxQP;
  bool bRelative = (eMode & AL_GENERATE_RELATIVE_QP);

  if(((AL_EGenerateQpMode)(eMode & AL_GENERATE_QP_TABLE_MASK)) == AL_GENERATE_LOAD_QP)
  {
    bool bIsAOM = false;

    uint8_t* pQPs = AL_Buffer_GetData(pQpBuf) + EP2_BUF_QP_TABLE.Offset;
    int32_t iNumQPPerLCU, iNumBytesPerLCU, iNumLCUs;
    GetQPBufferParameters(iLCUPicWidth, iLCUPicHeight, eProf, uLog2MaxCuSize, iQPTableDepth, iNumQPPerLCU, iNumBytesPerLCU, iNumLCUs, pQPs);
    Set_Block_Feature(pQPs, iNumLCUs, iNumBytesPerLCU, iQPTableDepth);

    ifstream file;

    if(!OpenFile(sQPTablesFolder, iFrameID, QPTablesMotif, file))
      return AL_ERR_CANNOT_OPEN_FILE;

    bool bTagFound = false;
    AL_ERR Err;

    do
    {
      uint8_t* pQpData = nullptr;

      AL_ESliceType eType;

      if(GetTag(file, eType))
      {
        AL_TQpTableMetaData* pMeta = GetQpTableMetaData(pQpBuf);
        pQpData = AL_Buffer_GetDataChunk(pQpBuf, pMeta->tQpTable[eType].iChunkIdx) + pMeta->tQpTable[eType].uOffset;
        bTagFound = true;
      }
      else if(bTagFound)
        break;
      else
        pQpData = AL_Buffer_GetData(pQpBuf);

      Err = bIsAOM ? Load_QPTable_FromFile_AOM(file, pQpData, iNumLCUs, iNumQPPerLCU, iNumBytesPerLCU, iQPTableDepth, bRelative) :
            Load_QPTable_FromFile(file, pQpData, iNumLCUs, iNumQPPerLCU, iNumBytesPerLCU, iQPTableDepth);
    }
    while(Err == AL_SUCCESS && bTagFound);
    return Err;
  }

  return AL_SUCCESS;
}
