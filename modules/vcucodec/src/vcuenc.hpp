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

#include "CfgParser.h" // ConfigFile

struct LayerResources;
struct CIpDevice;
struct EncoderSink;

namespace cv {
namespace vcucodec {

class VCUEncoder : public Encoder
{
public:
    virtual ~VCUEncoder();
    VCUEncoder(const String& filename, const EncoderInitParams& params);

    virtual void write(InputArray frame) override;
    virtual bool set(int propId, double value) override;
    virtual double get(int propId) const override;

private:
    String filename_;
    EncoderInitParams params_;
    std::vector<std::unique_ptr<LayerResources>> pLayerResources;
    std::unique_ptr<EncoderSink> enc;
    std::shared_ptr<CIpDevice> pIpDevice;
    ConfigFile cfg;
};

}  // namespace vcucodec
}  // namespace cv
