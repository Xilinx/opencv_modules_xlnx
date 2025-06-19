#include "opencv2/vcucodec.hpp"
#include "private/vcuutils.hpp"

namespace cv {
namespace vcucodec {

class VCUEncoder : public Encoder
{
public:
    virtual ~VCUEncoder() = default;
    VCUEncoder(const String& filename, const EncoderInitParams& params) : filename_(filename), params_(params) {}

    virtual void write(InputArray frame) {}

private:
    String filename_;
    EncoderInitParams params_;
};

Ptr<Encoder> createEncoder(const String& filename, const EncoderInitParams& params)
{
    Ptr<VCUEncoder> encoder = makePtr<VCUEncoder>(filename, params);
    return encoder;
}

}  // namespace vcucodec
}  // namespace cv 