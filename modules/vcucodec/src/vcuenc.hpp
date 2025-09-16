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

#include "private/vcuenccontext.hpp"

namespace cv {
namespace vcucodec {
class Device;
class VCUEncoder : public Encoder
{
public:
    struct Settings {
        RCSettings rc_;
        GOPSettings gop_;
        GlobalMotionVector gmv_;
        ProfileSettings profile_;
    };

    virtual ~VCUEncoder();
    VCUEncoder(const String& filename, const EncoderInitParams& params, Ptr<EncoderCallback> callback);

    virtual void write(InputArray frame) override;
    virtual bool eos() override;

    virtual bool set(int propId, double value) override;
    virtual double get(int propId) const override;

    virtual void set(const RCSettings& rcSettings) override;
    virtual void get(RCSettings& rcSettings) const override;
    virtual void set(const GOPSettings& gopSettings) override;
    virtual void get(GOPSettings& gopSettings) const override;
    virtual void set(const GlobalMotionVector& gmVector) override;
    virtual void get(GlobalMotionVector& gmVector) const override;
    virtual void set(const ProfileSettings& profileSettings) override;
    virtual void get(ProfileSettings& profileSettings) const override;

private:
    String filename_;
    EncoderInitParams params_;
    Ptr<EncoderCallback> callback_;
    Ptr<EncContext> enc_;
    Ptr<Device> device_;
    Ptr<EncContext::Config> cfg_;
    mutable std::mutex settingsMutex_;
    Settings currentSettings_;
};

}  // namespace vcucodec
}  // namespace cv
