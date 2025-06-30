#include "opencv2/vcucodec.hpp"

namespace cv {
namespace vcucodec {

class VCUDecoder : public Decoder
{
public:
    virtual ~VCUDecoder();

    VCUDecoder(const String& filename, const DecoderInitParams& params);

    // Implement the pure virtual function from base class
    virtual bool nextFrame(OutputArray frame, RawInfo& frame_info) override;
    virtual bool set(int propId, double value) override;
    virtual double get(int propId) const override;

private:
    void cleanup();

    String filename_;
    DecoderInitParams params_;
    bool vcu2_available_ = false;
    bool initialized_ = false;
};



} // namespace vcucodec
} // namespace cv