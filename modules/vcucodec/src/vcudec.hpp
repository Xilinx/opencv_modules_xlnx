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

#include "opencv2/vcucodec.hpp"
#include "vcudeccontext.hpp"

#include <map>
#include <mutex>

namespace cv {
namespace vcucodec {

class VCUDecoder : public Decoder
{
public:
    virtual ~VCUDecoder();
    VCUDecoder(const String& filename, const DecoderInitParams& params);

    // Implementation of the pure virtual functions from base class
    virtual bool   nextFrame(OutputArray frame, RawInfo& frame_info) override;
    virtual bool   nextFramePlanes(OutputArrayOfArrays planes, RawInfo& frame_info, bool byRef) override;
    virtual bool   set(int propId, double value) override;
    virtual double get(int propId) const override;
    virtual String streamInfo() const override;
    virtual String statistics() const override;

private:
    void   cleanup();
    void   copyToDestination(OutputArray dst, std::vector<Mat>& src, int fourccConvert,
                             bool vector_output, bool single_output_buffer, bool by_reference, int bit_depth);
    void   retrieveVideoFrame(OutputArray dst, Ptr<Frame> frame, RawInfo& frame_info,
                              bool vector_output, bool by_reference);
    void   updateRawInfo(RawInfo& frame_info);
    bool   setCaptureProperty(int propId, double value, bool external);
    double getCaptureProperty(int propId) const;

    String filename_;
    DecoderInitParams params_;
    bool vcu2_available_ = false;
    bool initialized_ = false;
    WorkerConfig wCfg = {nullptr, nullptr};
    Ptr<RawOutput> rawOutput_ = nullptr;
    std::shared_ptr<DecContext> decodeCtx_ = nullptr;
    RawInfo rawInfo_;
    std::mutex rawInfoMutex_;
    std::map<int, double> captureProperties_;
    mutable std::mutex capturePropertiesMutex_;
    uint32_t frameIndex_ = 0;
};



} // namespace vcucodec
} // namespace cv
