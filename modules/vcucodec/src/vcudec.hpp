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

#include "ctrlsw_dec.hpp"
#include "opencv2/vcucodec.hpp"

namespace cv {
namespace vcucodec {

class VCUDecoder : public Decoder
{
public:
    virtual ~VCUDecoder();
    VCUDecoder(const String& filename, const DecoderInitParams& params);

    // Implementation of the pure virtual functions from base class
    virtual bool   nextFrame(OutputArray frame, RawInfo& frame_info) override;
    virtual bool   set(int propId, double value) override;
    virtual double get(int propId) const override;

private:
    void cleanup();
    void retrieveVideoFrame(OutputArray dst, AL_TBuffer* pFrame, RawInfo& frame_info);

    String filename_;
    DecoderInitParams params_;
    bool vcu2_available_ = false;
    bool initialized_ = false;
    WorkerConfig wCfg = {nullptr, nullptr};
    std::shared_ptr<DecoderContext> decodeCtx_ = nullptr;
};



} // namespace vcucodec
} // namespace cv
