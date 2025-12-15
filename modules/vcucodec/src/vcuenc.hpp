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

#include "vcuenccontext.hpp"
#include "vcucommand.hpp"
#include "vcuutils.hpp"

extern "C"
{
#include "lib_encode/lib_encoder.h"
}

namespace cv {
namespace vcucodec {
class Device;
class VCUEncoder : public Encoder
{
public:
    // Input mode tracking - write() and writeFile() are mutually exclusive
    enum class InputMode { NONE, FRAME, FILE };

    struct Settings {
        PictureEncSettings pic_;
        RCSettings rc_;
        GOPSettings gop_;
        GlobalMotionVector gmv_;
        ProfileSettings profile_;
    };

    virtual ~VCUEncoder();
    VCUEncoder(const String& filename, const EncoderInitParams& params,
               Ptr<EncoderCallback> callback = nullptr);


    void init(const EncoderInitParams& params, Ptr<EncoderCallback> callback);

    virtual void write(InputArray frame) override;
    virtual void writeFile(const String& filename, int startFrame = 0, int numFrames = 0) override;
    virtual bool eos() override;
    virtual String settings() const override;
    virtual String statistics() const override;

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

    //
    // Dynamic commands
    //

    virtual void setSceneChange(int32_t frameIdx, int32_t lookAhead) override;
    virtual void setIsLongTerm(int32_t frameIdx) override;
    virtual void setUseLongTerm(int32_t frameIdx) override;
    virtual void restartGop(int32_t frameIdx) override;
    virtual void restartGopRecoveryPoint(int32_t frameIdx) override;
    virtual void setGopLength(int32_t frameIdx, int32_t gopLength) override;
    virtual void setNumB(int32_t frameIdx, int32_t numB) override;
    virtual void setFreqIDR(int32_t frameIdx, int32_t freqIDR) override;
    virtual void setFrameRate(int32_t frameIdx, int32_t frameRate, int32_t clockRatio) override;
    virtual void setBitRate(int32_t frameIdx, int32_t bitRate) override;
    virtual void setMaxBitRate(int32_t frameIdx, int32_t iTargetBitRate, int32_t iMaxBitRate) override;
    virtual void setQP(int32_t frameIdx, int32_t qp) override;
    virtual void setQPOffset(int32_t frameIdx, int32_t iQpOffset) override;
    virtual void setQPBounds(int32_t frameIdx, int32_t iMinQP, int32_t iMaxQP) override;
    virtual void setQPBoundsI(int32_t frameIdx, int32_t iMinQP_I, int32_t iMaxQP_I) override;
    virtual void setQPBoundsP(int32_t frameIdx, int32_t iMinQP_P, int32_t iMaxQP_P) override;
    virtual void setQPBoundsB(int32_t frameIdx, int32_t iMinQP_B, int32_t iMaxQP_B) override;
    virtual void setQPIPDelta(int32_t frameIdx, int32_t iQPDelta) override;
    virtual void setQPPBDelta(int32_t frameIdx, int32_t iQPDelta) override;
    virtual void setLFMode(int32_t frameIdx, int32_t iMode) override;
    virtual void setLFBetaOffset(int32_t frameIdx, int32_t iBetaOffset) override;
    virtual void setLFTcOffset(int32_t frameIdx, int32_t iTcOffset) override;
    virtual void setCostMode(int32_t frameIdx, bool bCostMode) override;
    virtual void setMaxPictureSize(int32_t frameIdx, int32_t iMaxPictureSize) override;
    virtual void setMaxPictureSizeI(int32_t frameIdx, int32_t iMaxPictureSize_I) override;
    virtual void setMaxPictureSizeP(int32_t frameIdx, int32_t iMaxPictureSize_P) override;
    virtual void setMaxPictureSizeB(int32_t frameIdx, int32_t iMaxPictureSize_B) override;
    virtual void setQPChromaOffsets(int32_t frameIdx, int32_t iQp1Offset, int32_t iQp2Offset) override;
    virtual void setAutoQP(int32_t frameIdx, bool bUseAutoQP) override;
    virtual void setHDRIndex(int32_t frameIdx, int32_t iHDRIdx) override;
    virtual void setAutoQPThresholdQPAndDeltaQP(int32_t frameIdx, bool bEnableUserAutoQPValues,
            std::vector<int> thresholdQP, std::vector<int> deltaQP) override;
    virtual void setIsSkip(int32_t frameIdx) override;
    virtual void setSAO(int32_t frameIdx, bool bSAOEnabled) override;

private:
    bool validateSettings();
    void initSettings(const EncoderInitParams& params);
    String currentSettingsString() const;

    String filename_;
    EncoderInitParams params_;
    Ptr<EncoderCallback> callback_;
    Ptr<EncContext> enc_;
    Ptr<Device> device_;
    Ptr<EncContext::Config> cfg_;
    std::unique_ptr<FormatInfo> srcFormatInfo_;
    mutable std::mutex settingsMutex_;
    Settings currentSettings_;
    String settingsString_;
    CommandQueue commandQueue_;
    int32_t currentFrameIndex_;
    AL_HEncoder hEnc_;
    InputMode inputMode_{InputMode::NONE};
};

}  // namespace vcucodec
}  // namespace cv
