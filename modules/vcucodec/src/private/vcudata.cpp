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
#include "vcudata.hpp"

extern "C"
{
#include "config.h"
#include <lib_encode/lib_encoder.h>
#include <lib_common/BufferStreamMeta.h>
}

namespace cv {
namespace vcucodec {

namespace { // anonymous

void fillSectionFillerData(AL_TBuffer* pStream, int32_t iSection)
{
  auto pStreamMeta = (AL_TStreamMetaData*)AL_Buffer_GetMetaData(pStream, AL_META_TYPE_STREAM);
  AL_TStreamSection* pSection = &pStreamMeta->pSections[iSection];

  uint32_t uLength = pSection->uLength;

  if(uLength <= 4)
    throw std::runtime_error("Section length(" + std::to_string(uLength) + ") must be higher than 4");

  uint8_t* pData = AL_Buffer_GetData(pStream) + pSection->uOffset;

  while(--uLength && (*pData != 0xFF))
    ++pData;

  if(uLength > 0)
    Rtos_Memset(pData, 0xFF, uLength);

  if(pData[uLength] != 0x80)
    throw std::runtime_error("pData[uLength] must end with 0x80");
}

} // anonymous namespace


Data::Data(AL_TBuffer* data, AL_HEncoder hEnc) : data_(data), hEnc_(hEnc)
{

}

Data::~Data()
{
    if (data_ != nullptr)
    {
        bool ret = AL_Encoder_PutStreamBuffer(hEnc_, data_);
        if (!ret)
            fprintf(stderr, "AL_Encoder_PutStreamBuffer must always succeed\n");
    }
}

int32_t Data::walkBuffers(std::function<void(size_t size, uint8_t* data)> callback) const
{
    int32_t nrFrames = 0;
    if (data_ != nullptr)
    {
        AL_TStreamMetaData* pStreamMeta =
            (AL_TStreamMetaData*)AL_Buffer_GetMetaData(data_, AL_META_TYPE_STREAM);

        for(int32_t curSection = 0; curSection < pStreamMeta->uNumSection; ++curSection)
        {
            AL_TStreamSection* pCurSection = &pStreamMeta->pSections[curSection];

            if(pCurSection->eFlags & AL_SECTION_END_FRAME_FLAG)
                ++nrFrames;

            if(pCurSection->eFlags & AL_SECTION_APP_FILLER_FLAG)
                fillSectionFillerData(data_, curSection);

            uint8_t* pData = AL_Buffer_GetData(data_);
            if(pCurSection->uLength)
            {
                uint32_t remainder = AL_Buffer_GetSize(data_) - pCurSection->uOffset;
                if(remainder < pCurSection->uLength)
                {
                    callback(remainder, pData + pCurSection->uOffset);
                    callback(pCurSection->uLength - remainder, pData);
                }
                else
                {
                    callback(pCurSection->uLength, pData + pCurSection->uOffset);
                }
            }
        }
    }
    return nrFrames;
}

/*static*/
Ptr<Data> Data::create(AL_TBuffer* buffer, AL_HEncoder hEnc)
{
    auto data {new Data(buffer, hEnc)};
    return data;
}


} // namespace vcucodec
} // namespace cv