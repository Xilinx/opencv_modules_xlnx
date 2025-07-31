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
#include "vcureader.hpp"

#include <fstream>
#include <thread>
#include <atomic>


namespace cv {
namespace vcucodec {


class FileReader : public Reader
{
public:
    FileReader(AL_HDecoder hDec, BufPool& bufPool)
    : hDec_(hDec), bufPool_(bufPool)  {}

    ~FileReader() override
    {
        if (thread_.joinable())
            thread_.join();
        if (fp_.is_open())
            fp_.close();
    }

    bool setPath(std::string_view filePath) override
    {
        filePath_ = filePath;
        fp_.open(filePath_, std::ios::binary);
        opened_ = fp_.is_open();
        return opened_;
    }

    void start() override
    {
        if (!opened_)
        {
            CV_Error(cv::Error::StsBadArg, "Stream input must be opened");
        }
        thread_ = std::thread(&FileReader::run, this);
    }

    void stop() override
    {
        stopping_ = true;
    }

    /// Implementation for running the file reading in a separate thread
    void run()
    {
        Rtos_SetCurrentThreadName("FileReader");
        while (!stopping_) {
            std::shared_ptr<AL_TBuffer> pInputBuf;
            try
            {
                pInputBuf = bufPool_.GetSharedBuffer();
            }
            catch(bufpool_decommited_error &)
            {
                continue;
            }
            uint8_t* pBuf = AL_Buffer_GetData(pInputBuf.get());

            fp_.read((char*)pBuf, AL_Buffer_GetSize(pInputBuf.get()));
            auto nrBytes = fp_.gcount();
            uint8_t uBufFlags = AL_STREAM_BUF_FLAG_UNKNOWN;
            if (nrBytes == 0)
            {
                stopping_ = true;
                AL_Decoder_Flush(hDec_);
                break;
            }
            else
            {
                if (!AL_Decoder_PushStreamBuffer(hDec_, pInputBuf.get(), nrBytes, uBufFlags))
                {
                    throw std::runtime_error("Failed to push buffer to decoder");
                }
            }
        }
    }

private:
    std::string   filePath_;
    AL_HDecoder   hDec_;
    BufPool&      bufPool_;
    std::ifstream fp_;
    std::thread   thread_;
    std::atomic<bool>  opened_{false};
    std::atomic<bool>  stopping_{false};
};

/*static*/ std::unique_ptr<Reader> Reader::createReader(AL_HDecoder hDec, BufPool& bufPool)
{
    return std::unique_ptr<Reader>(new FileReader(hDec, bufPool));
}

} // namespace vcucodec
} // namespace cv
